"""Standalone ADWV1 PC compiler/reference consumer. Python standard library; optional Pillow chart."""
import argparse, bisect, collections, csv, hashlib, json, random, struct, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAGIC = b'ADWV1\0\0\0'
HEADER = struct.Struct('<8s14I')
EVENT = struct.Struct('<IHBBBBH')
CAP = struct.Struct('<HBB')
NONE = 65535
FIELDS = ['version','header_bytes','record_bytes','event_count','duration_ms','events_offset','pressure_offset','pressure_count','index_offset','index_count','index_step_ms','raw_bytes','body_crc32','spawn_end_ms']

def config():
    return json.loads((ROOT/'resources/waves/standard.json').read_text(encoding='utf-8'))

def compile_wave(c):
    assert c['entry_counts']==[430,430,15] and c['variants']==2
    assert c['index_step_ms']==1 and c['max_events_per_update']==1 and c['index_padding_ms']==2000
    assert c['suspend_at']==750 and c['resume_at']==720
    assert c['duration_ms']>=c['spawn_end_ms']>0
    num=c.get('source_rate_numerator',13); den=c.get('source_rate_denominator',10)
    assert (num,den)==(13,10)
    rng=random.Random(c['seed']); events=[]; end=0
    for start,stop,rate in c['phases']:
        assert start==end and stop>start and isinstance(rate,int) and 0<=rate<=48
        assert rate==0 or stop<=c['spawn_end_ms']
        end=stop
        # Exact start + floor(k*10000/(source_rate*13)), no bursts.
        k=0
        while rate and start+k*1000*den//(rate*num)<stop:
            t=start+k*1000*den//(rate*num)
            side=rng.choices(range(3),weights=c['side_weights'])[0]
            kind=rng.choices(range(3),weights=c['kind_weights_by_side'][side])[0]
            assert not (side==2 and kind==2)
            events.append((t,rng.randrange(c['entry_counts'][side]),kind,side,rng.randrange(2),0,0)); k+=1
    assert end==c['duration_ms'] and len(events)<NONE
    capacity=[]
    for n in range(1024):
        matches=[(interval,permit,int(n<=c['resume_at'])) for lo,hi,interval,permit in c['pressure_bands'] if lo<=n<hi]
        assert len(matches)==1
        capacity.append(matches[0])
    count=c['duration_ms']+c['index_padding_ms']+1
    lookup=[NONE]*count
    for i,e in enumerate(events):
        stop=events[i+1][0] if i+1<len(events) else c['spawn_end_ms']
        lookup[e[0]:stop]=[i]*(stop-e[0])
    eb=b''.join(EVENT.pack(*e) for e in events)
    cb=b''.join(CAP.pack(*v) for v in capacity)
    ib=struct.pack('<%dH'%count,*lookup)
    body=eb+cb+ib
    values=[1,64,12,len(events),c['duration_ms'],64,64+len(eb),1024,64+len(eb)+len(cb),count,1,64+len(body),zlib.crc32(body),c['spawn_end_ms']]
    return HEADER.pack(MAGIC,*values)+body

def decode(raw):
    if len(raw)<64: raise ValueError('short header')
    magic,*v=HEADER.unpack_from(raw); h=dict(zip(FIELDS,v))
    n=h['event_count']; po=64+12*n; io=po+4096
    if (magic!=MAGIC or h['version']!=1 or h['header_bytes']!=64 or h['record_bytes']!=12 or h['events_offset']!=64 or h['pressure_offset']!=po or h['pressure_count']!=1024 or h['index_offset']!=io or h['index_step_ms']!=1 or h['index_count']!=h['duration_ms']+2001 or h['raw_bytes']!=len(raw) or io+2*h['index_count']!=len(raw) or zlib.crc32(raw[64:])!=h['body_crc32']):
        raise ValueError('invalid schema/length/CRC')
    events=[EVENT.unpack_from(raw,64+12*i) for i in range(n)]
    for i,e in enumerate(events):
        t,entry,kind,side,variant,flags,reserved=e
        if not (side in (0,1,2) and kind in (0,1,2) and not(side==2 and kind==2) and entry<(15 if side==2 else 430) and variant<2 and flags==reserved==0 and t<h['spawn_end_ms']<=h['duration_ms'] and (not i or t>events[i-1][0])): raise ValueError('event invalid')
    cap=[CAP.unpack_from(raw,po+4*i) for i in range(1024)]
    if any(permit not in (0,1) or recover not in (0,1) or (permit and interval==0) or (recover and not permit) for interval,permit,recover in cap): raise ValueError('capacity invalid')
    idx=list(struct.unpack_from('<%dH'%h['index_count'],raw,io))
    for t,j in enumerate(idx):
        expected=bisect.bisect_right(events,(t,65535,255,255,255,255,65535))-1
        expected=expected if 0<=expected and t<h['spawn_end_ms'] else NONE
        if j!=expected: raise ValueError('index invalid')
    return h,events,cap,idx

class Consumer:
    """One lookup, <=one attempt per update. No backlog scan, no mutation of actors."""
    def __init__(self,decoded):
        self.h,self.events,self.cap,self.idx=decoded
        self.last_consumed=-1; self.last_spawn=None; self.latched=False
        self.executed=[]; self.rejected=0; self.attempted=0
    def update(self,game_ms,pressure):
        if pressure<0: raise ValueError('negative pressure')
        # Out-of-table values STOP spawning; actor count is never clamped.
        interval,permit,recover = self.cap[pressure] if pressure<=1023 else (0,0,0)
        if not permit: self.latched=True
        elif recover: self.latched=False
        if game_ms<0 or game_ms>=self.h['spawn_end_ms']: return None
        j=self.idx[game_ms]
        if j==NONE or j<=self.last_consumed: return None
        self.last_consumed=j; self.attempted+=1
        if pressure>1023: interval,permit=0,0
        else: interval,permit,_=self.cap[pressure]
        if self.latched or not permit or (self.last_spawn is not None and game_ms-self.last_spawn<interval):
            self.rejected+=1; return None
        self.last_spawn=game_ms; self.executed.append((game_ms,j)); return self.events[j]

def simulate(d,step,pressure,gap=False):
    s=Consumer(d)
    for t in range(0,d[0]['duration_ms']+1,step):
        if gap and 30000<=t<40000: continue
        s.update(t,pressure)
    return {'step_ms':step,'constant_pressure':pressure,'long_gap_30_40s':gap,'scheduled':len(s.events),'executed':len(s.executed),'suppressed':len(s.events)-len(s.executed),'pressure_or_interval_rejected':s.rejected,'expired_unobserved':len(s.events)-s.attempted,'max_spawn_per_update':1 if s.executed else 0}

def write_json(path,obj):
    path.write_text(json.dumps(obj,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def draw(c,events,out=None):
    from PIL import Image,ImageDraw,ImageFont
    im=Image.new('RGB',(1400,700),'#102030'); dr=ImageDraw.Draw(im)
    font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',22); title=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',34)
    dr.text((65,30),'ADWV1 | one wave / PC-only schedule',font=title,fill='white')
    peak=max(r for a,b,r in c['phases'])*13/10; scale=max(40,peak)
    dr.text((65,85),f'{len(events)} scheduled | peak {peak}/s | {c["duration_ms"]//1000}s game clock | seed {c["seed"]}',font=font,fill='#add7eb')
    x0,y0,w,h=90,490,1200,300
    for r in (0,10,20,30,40):
        y=y0-r*h/scale; dr.line((x0,y,x0+w,y),fill='#345063'); dr.text((35,y-12),str(r),font=font,fill='white')
    for start,stop,rate in c['phases']:
        rate=rate*13/10
        a=x0+start*w/c['duration_ms']; b=x0+stop*w/c['duration_ms']
        if rate: dr.rectangle((a,y0-rate*h/scale,b-2,y0),fill='#40b7ae')
        else: dr.rectangle((a,170,b,490),outline='#f6be66',width=3)
        dr.text((a+5,505),str(start//1000)+'s',font=font,fill='white')
        dr.text((a+5,145),str(rate)+'/s',font=font,fill='#f6be66')
    dr.text((65,570),f'0-{c["spawn_end_ms"]//1000}s: uniformly spaced events; missed slots expire, never catch up.',font=font,fill='white')
    dr.text((65,610),f'{c["spawn_end_ms"]//1000}-{c["duration_ms"]//1000}s: observe/clear only. No forced deletion. No automatic next wave.',font=font,fill='white')
    im.save((out or ROOT)/'wave_chart.png')

def main():
    p=argparse.ArgumentParser(description='Compile compatible ADWV1 raw-deflate resources')
    p.add_argument('--output',type=Path,default=ROOT.parent/'临时开发1-性能增强/g219/data/wave')
    p.add_argument('--install',action='store_true',help='Copy final streams and manifest to project resources/waves')
    p.add_argument('--chart',action='store_true',help='Optional Pillow visualization')
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    streams=[]
    for asset_id,name in enumerate(('standard','high')):
        c=json.loads((ROOT/'resources/waves'/f'{name}.json').read_text(encoding='utf-8'))
        raw=compile_wave(c); h,events,cap,idx=decode(raw)
        co=zlib.compressobj(9,zlib.DEFLATED,-15); packed=co.compress(raw)+co.flush()
        assert zlib.decompress(packed,-15)==raw
        target=args.output/name; target.mkdir(exist_ok=True)
        (target/'wave_events.bin').write_bytes(raw)
        (target/'wave.pack').write_bytes(packed)
        with (target/'events.csv').open('w',newline='',encoding='utf-8') as f:
            w=csv.writer(f); w.writerow(['time_ms','entry_id','kind','side','variant','flags','reserved']); w.writerows(events)
        row=dict(kind=9,id=asset_id,path=f'resources/waves/{name}.pack',codec=1,predictor=0,param=0,alignment=64,archive_only=False,raw_bytes=len(raw),packed_bytes=len(packed),raw_crc=zlib.crc32(raw),packed_crc=zlib.crc32(packed),decoded_sha256=hashlib.sha256(raw).hexdigest(),packed_sha256=hashlib.sha256(packed).hexdigest())
        streams.append(row)
        write_json(target/'report.json',dict(header=h,scheduled=len(events),peak=max(r for a,b,r in c['phases'])*13/10,stream=row))
        if args.chart: draw(c,events,target)
        if args.install: (ROOT/row['path']).write_bytes(packed)
        print(json.dumps(dict(name=name,scheduled=len(events),**row)))
    manifest=dict(schema=1,streams=streams)
    write_json(args.output/'external_streams.json',manifest)
    if args.install: write_json(ROOT/'resources/waves/external_streams.json',manifest)

if __name__=='__main__': main()
