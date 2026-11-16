"""Derive external directory input without generating or modifying actual assets."""
from pathlib import Path
import json,argparse,re
from pack_flash import decoded,crc,sha,require

def create(root):
    load=lambda rel:json.loads((root/rel).read_text(encoding='utf-8-sig'))
    entries=[]
    text=lambda rel:(root/rel).read_text(encoding='utf-8-sig')
    table=re.search(r'runtime_groups\[\]\s*=\s*\{(.*?)\};',text('src/runtime_data.c'),re.S).group(1)
    names=re.findall(r'&(\w+)',table);header=text('resources/player/fpga_player.h');require(len(names)==38,'player group count')
    for i,name in enumerate(names):
        m=re.search(r'FpgaPlayerGroup\s+'+re.escape(name)+r'\s*=\s*\{(\d+),(\d+),(\d+),',header);require(m is not None,'missing player metadata')
        off,packed,raw=map(int,m.groups());entries.append(dict(kind=1,id=i,path='resources/player/player.pack',source_offset=off,packed_bytes=packed,raw_bytes=raw,codec=1,predictor=0,alignment=64))
    arrow=re.search(r'arrow_assets\[104\]\s*=\s*\{(.*?)\};',text('src/arrow_data.c'),re.S).group(1)
    rows=re.findall(r'\{([^{}]+)\}',arrow);require(len(rows)==104,'arrow group count')
    for i,row in enumerate(rows):
        off,packed,raw_off,w,h,ax,ay=map(int,row.split(','));entries.append(dict(kind=2,id=i,path='resources/arrows/arrows.pack',source_offset=off,packed_bytes=packed,raw_bytes=w*h*2,codec=1,predictor=0,alignment=64))
    scene=re.search(r'groups\[9\]\s*=\s*\{(.*?)\};',text('src/scene_data.c'),re.S).group(1)
    rows=re.findall(r'\{([^{}]+)\}',scene);require(len(rows)==9,'scene group count')
    for i,row in enumerate(rows):
        dst,raw,src,packed,mode,ref=map(int,row.split(','))
        if mode==4:continue
        entries.append(dict(kind=3,id=i,path='resources/scene/scene.pack',source_offset=src,packed_bytes=packed,raw_bytes=raw,codec=1,predictor=int(mode!=0),alignment=64))
    for s in load('resources/dragon/external_streams.json')['streams']:
        require(s['kind']==4,'dragon schema not frozen to kind4 ids0,1')
        entries.append(dict(kind=s['kind'],id=s['id'],path=s['path'],codec=s['codec'],predictor=s['predictor'],raw_bytes=s['raw_bytes'],raw_crc32=s['raw_crc'],alignment=s['alignment'],archive_only=s['archive_only']))
    flow=load('resources/flow/external_streams.json')['streams']
    require(len(flow)==1 and flow[0]['kind']==5 and flow[0]['id']==0,'flow requires unique kind5 id0 stream')
    for s in flow:
        require(s['predictor']==0 and not s['archive_only'],'flow must be direct startup data')
        entries.append(dict(kind=s['kind'],id=s['id'],path=s['path'],codec=s['codec'],predictor=s['predictor'],raw_bytes=s['raw_bytes'],raw_crc32=s['raw_crc'],alignment=s['alignment'],archive_only=s['archive_only']))
    countdown=load('resources/countdown/external_streams.json')['streams']
    require(len(countdown)==1 and countdown[0]['kind']==6 and countdown[0]['id']==0,'countdown requires unique kind6 id0 stream')
    for s in countdown:
        require(s['codec']==1 and s['predictor']==0 and not s['archive_only'],'countdown must be direct startup DEFLATE data')
        entries.append(dict(kind=s['kind'],id=s['id'],path=s['path'],codec=s['codec'],predictor=s['predictor'],raw_bytes=s['raw_bytes'],raw_crc32=s['raw_crc'],alignment=s['alignment'],archive_only=s['archive_only']))
    enemy=load('resources/enemy/external_streams.json')
    require(enemy['schema']==1,'enemy external schema')
    streams=enemy['streams']
    require(len(streams)==1 and streams[0]['kind']==7 and streams[0]['id']==0,'enemy requires unique kind7 id0 stream')
    for s in streams:
        require(s['codec']==1 and s['predictor']==0 and not s['archive_only'] and s.get('param',0)==0,'enemy must be direct startup DEFLATE data')
        packed=(root/s['path']).read_bytes(); raw=decoded(packed,s['codec'])
        require(crc(packed)==s['packed_crc'] and sha(raw)==s['decoded_sha256'],'enemy external source packedCRC/decodedSHA mismatch')
        entries.append(dict(kind=s['kind'],id=s['id'],path=s['path'],codec=s['codec'],predictor=s['predictor'],raw_bytes=s['raw_bytes'],raw_crc32=s['raw_crc'],alignment=s['alignment'],archive_only=s['archive_only']))
    route_source=load('resources/precomputed/external_streams.json')
    require(route_source['schema']==1,'precomputed external schema')
    routes=route_source['streams']
    require(len(routes)==1 and routes[0]['kind']==8 and routes[0]['id']==0,'precomputed requires unique kind8 id0 stream')
    s=routes[0];require(s['codec']==1 and s['predictor']==0 and not s['archive_only'],'precomputed must be direct startup DEFLATE data')
    packed=(root/s['path']).read_bytes();raw=decoded(packed,s['codec'])
    require(s.get('param',0)==0 and s['alignment']==64,'precomputed reserved/alignment mismatch')
    require((root/s['path']).resolve().is_relative_to(root.resolve()),'precomputed source outside project')
    require(len(packed)==s['packed_bytes'] and len(raw)==s['raw_bytes'],'precomputed lengths mismatch')
    require(crc(packed)==s['packed_crc'] and sha(raw)==s['decoded_sha256'] and sha(packed)==s['packed_sha256'],'precomputed CRC/SHA mismatch')
    entries.append(dict(kind=s['kind'],id=s['id'],path=s['path'],codec=s['codec'],predictor=s['predictor'],raw_bytes=s['raw_bytes'],raw_crc32=s['raw_crc'],alignment=s['alignment'],archive_only=s['archive_only']))
    require(len(entries)==156,'G219 inherited directory must contain 156 entries')
    waves=load('resources/waves/external_streams.json')
    require(waves['schema']==1 and [(s['kind'],s['id']) for s in waves['streams']]==[(9,0),(9,1)],'wave requires kind9 ids0/1')
    for s in waves['streams']:
        require(s['codec']==1 and s['predictor']==0 and s.get('param',0)==0 and s['alignment']==64 and not s['archive_only'],'wave container contract')
        require((root/s['path']).resolve().is_relative_to(root.resolve()),'wave source outside project')
        packed=(root/s['path']).read_bytes();raw=decoded(packed,s['codec'])
        require(len(packed)==s['packed_bytes'] and len(raw)==s['raw_bytes'],'wave lengths mismatch')
        require(crc(packed)==s['packed_crc'] and crc(raw)==s['raw_crc'] and sha(raw)==s['decoded_sha256'] and sha(packed)==s['packed_sha256'],'wave CRC/SHA mismatch')
        require(raw[:8]==b'ADWV1\x00\x00\x00','wave container magic')
        entries.append(dict(kind=9,id=s['id'],path=s['path'],codec=1,predictor=0,raw_bytes=s['raw_bytes'],raw_crc32=s['raw_crc'],alignment=64,archive_only=False))
    require(len(entries)==158,'G222 inherited directory requires 158 consumer entries')
    hud=load('resources/hud/external_streams.json')
    require(hud['schema']==1 and [(s['kind'],s['id']) for s in hud['streams']]==[(10,0)],'HUD requires unique kind10 id0')
    s=hud['streams'][0]
    require(s['codec']==1 and s['predictor']==0 and s.get('param',0)==0 and s['alignment']==64 and not s['archive_only'],'HUD container contract')
    require((root/s['path']).resolve().is_relative_to(root.resolve()),'HUD source outside project')
    packed=(root/s['path']).read_bytes();raw=decoded(packed,s['codec'])
    require(len(packed)==s['packed_bytes'] and len(raw)==s['raw_bytes'],'HUD lengths mismatch')
    require(crc(packed)==s['packed_crc'] and crc(raw)==s['raw_crc'] and sha(raw)==s['decoded_sha256'] and sha(packed)==s['packed_sha256'],'HUD CRC/SHA mismatch')
    entries.append(dict(kind=10,id=0,path=s['path'],codec=1,predictor=0,raw_bytes=s['raw_bytes'],raw_crc32=s['raw_crc'],alignment=64,archive_only=False))
    require(len(entries)==159,'G225 requires 159 consumer entries including current HUD10')
    for e in entries:
        data=(root/e['path']).read_bytes()
        if 'source_offset' in e:data=data[e['source_offset']:e['source_offset']+e['packed_bytes']]
        raw=decoded(data,e['codec']);require(len(raw)==e['raw_bytes'],'raw manifest mismatch')
        if 'raw_crc32' in e:require(e['raw_crc32']==crc(raw),'external rawCRC mismatch')
        e['raw_crc32']=crc(raw)
    return dict(project='G2.25_V10Display',map_version='G225-ADFLASH3-159-ADWV1-HUD10',schema='ADFLASH3',entries=entries,consumers=[{k:e[k] for k in ('kind','id','codec','predictor','raw_bytes')} for e in entries])
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);p.add_argument('--check',action='store_true');a=p.parse_args();root=Path(__file__).resolve().parents[1]
    require(a.output.resolve().parent==root,'manifest output must be project root (paths root-relative)')
    current=create(root)
    if a.check:require(json.loads(a.output.read_text(encoding='utf-8-sig'))==current,'frozen consumer manifest differs from current streams')
    else:a.output.write_text(json.dumps(current,indent=2)+'\n',encoding='utf-8')
