#!/usr/bin/env python3
"""PTR9v1 reproducible route compiler. Only final consumer pack/manifest enter resources.
Full script is self-contained: copy this file into release scripts, not the dev shim.
"""
import argparse, hashlib, heapq, json, math, struct, zlib
from pathlib import Path
from functools import lru_cache
BASE=[(960,70,1180,130),(758,108,782,122),(848,108,872,122)]
WALL=[(0,183,440,217),(840,183,1280,217),(0,420,520,454),(760,420,1280,454)]
BOTTOM=[170,232,294,356,418,480,542,640,738,800,862,924,986,1048,1110]
GOAL=(640,70)
def inside(p,r):
    x,y=p;l,t,r,b=r;return l<=x<r and t<=y<b
def hit(a,b,r):
    # Closed expanded slab intersection: conservative w.r.t half-open scene.
    lo,hi=0.,1.
    for p,d,mn,mx in ((a[0],b[0]-a[0],r[0],r[2]),(a[1],b[1]-a[1],r[1],r[3])):
        if not d:
            if p<mn or p>mx:return False
        else:
            q,s=(mn-p)/d,(mx-p)/d
            lo=max(lo,min(q,s));hi=min(hi,max(q,s))
            if lo>hi:return False
    return True
def clear(a,b,wall,margin=3):
    if not all(60<=p[0]<1220 and 70<=p[1]<580 for p in (a,b)):return False
    return not any(hit(a,b,(l-margin,t-margin,r+margin,h+margin)) for l,t,r,h in BASE+(WALL if wall else []))
@lru_cache(None)
def graph(wall):
    nodes=[GOAL]
    for l,t,r,b in BASE+(WALL if wall else []):
        for x in (l-7,r+7):
            for y in (t-7,b+7):
                if clear((x,y),(x,y),wall):nodes.append((x,y))
    adj=[[] for _ in nodes]
    for i,a in enumerate(nodes):
        for j,b in enumerate(nodes[:i]):
            if clear(a,b,wall):
                d=math.dist(a,b);adj[i].append((j,d));adj[j].append((i,d))
    dist=[math.inf]*len(nodes);nxt=[None]*len(nodes);dist[0]=0;heap=[(0,0)]
    while heap:
        d,i=heapq.heappop(heap)
        if d!=dist[i]:continue
        for j,w in adj[i]:
            if d+w<dist[j]:dist[j]=d+w;nxt[j]=i;heapq.heappush(heap,(dist[j],j))
    return nodes,dist,nxt
@lru_cache(None)
def gatepath(p,wall):
    nodes,ds,nxt=graph(wall)
    choices=[(math.dist(p,q)+ds[i],i) for i,q in enumerate(nodes) if math.isfinite(ds[i]) and clear(p,q,wall)]
    if not choices:raise RuntimeError(('unreachable',p,wall))
    _,i=min(choices);out=[p]
    while True:
        out.append(nodes[i])
        if i==0:break
        i=nxt[i]
    return tuple(out)
def bend(path,wall,variant):
    out=[path[0]]
    # Shared exact endpoints and obstacle corner gates. Offset midpoint only
    # when BOTH halves have >=8px clearance. No offsetting spawn coordinates.
    for a,b in zip(path,path[1:]):
        d=math.dist(a,b)
        if d>60:
            sign=1 if variant else -1
            m=((a[0]+b[0])/2-sign*(b[1]-a[1])*5/d,(a[1]+b[1])/2+sign*(b[0]-a[0])*5/d)
            q=(a[0]*.75+b[0]*.25,a[1]*.75+b[1]*.25)
            t=(a[0]*.25+b[0]*.75,a[1]*.25+b[1]*.75)
            if clear(q,m,wall,8) and clear(m,t,wall,8):out.extend((q,m,t))
        out.append(b)
    return out
def route(kind,side,e,v,wall):
    start=((30 if side==0 else 1250),150+e) if side<2 else (BOTTOM[e],605)
    if side<2:
        y=min(start[1],570)
        if wall:
            for t,b in ((183,217),(420,454)):
                if t-6<=y<=b+6:y=t-7 if y<(t+b)/2 else b+7
        prefix=[start]
        if y!=start[1]:prefix.append((start[0],y))
        prefix.append((70 if side==0 else 1210,y))
    else:prefix=[start,(start[0],570)]
    path=prefix[:-1]+bend(gatepath(prefix[-1],wall),wall,v)
    # Continuous arc length, NOT one DDA pixel per simulation tick.
    distance_numerator=(126 if kind==1 else 105)*3*20
    distance_denominator=5*1000
    delta=distance_numerator/distance_denominator
    lens=[math.dist(a,b) for a,b in zip(path,path[1:])];total=sum(lens)
    points=[];seg=0;before=0.
    for n in range(math.ceil(total/delta)+1):
        d=min((n*distance_numerator)/distance_denominator,total)
        while seg<len(lens)-1 and d>before+lens[seg]:before+=lens[seg];seg+=1
        a,b=path[seg],path[seg+1];f=(d-before)/lens[seg] if lens[seg] else 0
        p=(round(a[0]+(b[0]-a[0])*f),round(a[1]+(b[1]-a[1])*f));points.append(p)
        if math.dist(p,GOAL)<=20:break
    return points,total

def validate(points,wall):
    entered=False
    for a,b in zip(points,points[1:]):
        # <=1px Euclidean DDA increment; scan endpoints and entire segment.
        steps=max(1,math.ceil(math.dist(a,b)))
        for k in range(steps+1):
            p=(a[0]+(b[0]-a[0])*k/steps,a[1]+(b[1]-a[1])*k/steps)
            x,y=p
            ground=60<=x<1220 and 70<=y<580
            if entered and not ground:raise AssertionError(('exit ground',p))
            entered |= ground
            if not(0<=x<1280 and 0<=y<630):raise AssertionError(('bounds',p))
            if any(inside(p,r) for r in BASE):raise AssertionError(('base',p))
            if wall and 60<=x<1220 and any(inside(p,r) for r in WALL):raise AssertionError(('wall',p))
        # Independent continuous slab for obstacle crossings in each stored segment.
        for r in BASE+(WALL if wall else []):
            if r in WALL and (max(a[0],b[0])<60 or min(a[0],b[0])>=1220):continue
            if hit(a,b,(r[0],r[1],r[2]-1e-7,r[3]-1e-7)):raise AssertionError(('analytic',a,b,r))
    assert math.dist(points[-1],GOAL)<=20
    assert max(math.dist(a,b) for a,b in zip(points,points[1:]))<=4.25

def main():
    here=Path(__file__).resolve().parent
    default_work=here.parents[1]/'临时开发1-性能增强/g219/data/routes' if here.name=='scripts' else here
    ap=argparse.ArgumentParser();ap.add_argument('--project',type=Path,help='Optional project label; never writes project resources automatically');ap.add_argument('--work',type=Path,default=default_work);ap.add_argument('--out',type=Path,help='Final consumer output directory; default WORK/final');args=ap.parse_args()
    work=args.work;work.mkdir(parents=True,exist_ok=True);res=args.out or work/'final';res.mkdir(parents=True,exist_ok=True)
    desc=bytearray();pool=bytearray();examples=[];counts={};maxstep=0;mincount=100000;maxcount=0
    for kind,wall,v in [(1,w,v) for w in range(2) for v in range(2)]+[(2,0,v) for v in range(2)]:
        for side in range(3 if kind==1 else 2):
            for e in range(15 if side==2 else 430):
                ps,total=route(kind,side,e,v,wall);validate(ps,wall)
                first=len(pool)//4;n=len(ps);pool.extend(b''.join(struct.pack('<HH',*p) for p in ps))
                desc.extend(struct.pack('<4B2H2I4H2I',kind,side,v,wall,e,0,first,n,*ps[0],*ps[-1],0xffffffff,1))
                counts[f'{kind}/{wall}/{v}/{side}']=counts.get(f'{kind}/{wall}/{v}/{side}',0)+1
                maxstep=max(maxstep,max(math.dist(a,b) for a,b in zip(ps,ps[1:])));mincount=min(mincount,n);maxcount=max(maxcount,n)
                if e in (0,33,50,67,270,287,304,429) or side==2:examples.append((kind,wall,v,side,e,ps))
    body=desc+pool;head=b'PTR9v1\0\0'+struct.pack('<14I',1,64,32,5220,64,64+len(desc),len(pool)//4,64+len(body),zlib.crc32(body),20,2,0,0,0)
    raw=head+body;co=zlib.compressobj(9,zlib.DEFLATED,-15);packed=co.compress(raw)+co.flush();assert zlib.decompress(packed,-15)==raw
    (work/'routes.raw').write_bytes(raw);(res/'routes.pack').write_bytes(packed)
    manifest={'format':'PTR9v1','kind':8,'id':0,'raw_bytes':len(raw),'packed_bytes':len(packed),'raw_crc32':zlib.crc32(raw),'body_crc32':zlib.crc32(body),'packed_crc32':zlib.crc32(packed),'raw_sha256':hashlib.sha256(raw).hexdigest(),'packed_sha256':hashlib.sha256(packed).hexdigest(),'descriptor_count':5220,'point_count':len(pool)//4,'coverage':counts,'min_points':mincount,'max_points':maxcount,'max_step_px':maxstep,'hazards':'none; runtime authoritative','generator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    (res/'routes_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');(work/'generation_report.json').write_text(json.dumps(manifest,indent=2)+'\n')
    streams={'schema':1,'streams':[dict(kind=8,id=0,path='resources/precomputed/routes.pack',codec=1,predictor=0,param=0,alignment=64,archive_only=False,raw_bytes=len(raw),packed_bytes=len(packed),raw_crc=zlib.crc32(raw),packed_crc=zlib.crc32(packed),decoded_sha256=hashlib.sha256(raw).hexdigest(),packed_sha256=hashlib.sha256(packed).hexdigest())]}
    (res/'external_streams.json').write_text(json.dumps(streams,indent=2)+'\n')
    (res/'routes_sizes.h').write_text(f'#ifndef PC_GENERATED_SIZES_H\n#define PC_GENERATED_SIZES_H\n#define PC_RAW_BYTES {len(raw)}u\n#define PC_RAW_CRC32 0x{zlib.crc32(raw):08x}u\n#endif\n')
    from PIL import Image,ImageDraw
    for wall in range(2):
        im=Image.new('RGB',(1280,630),'#f4f1e8');d=ImageDraw.Draw(im);d.rectangle((60,70,1219,579),outline='#888888');d.rectangle((330,250,949,389),fill='#bcd9ef')
        for r in BASE+(WALL if wall else []):d.rectangle(r,fill='#333333')
        for k,w,v,s,e,ps in examples:
            if k==1 and w==wall:d.line(ps,fill=('#e08a20' if v else '#21855a'),width=1)
        d.ellipse((620,50,660,90),outline='red',width=2);d.text((470,610),f'PTR9 wolf wall={wall}; green/orange variants',fill='black');im.save(work/f'routes-wall{wall}.png')
    print(json.dumps(manifest,indent=2))
if __name__=='__main__':main()
