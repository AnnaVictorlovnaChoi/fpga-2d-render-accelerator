"""ADFLASH3 host packer/strict verifier. Manifest entries reference packed streams, not outer containers."""
import argparse, hashlib, json, struct, zlib
from pathlib import Path
HEADER=struct.Struct('<8sIIIIQQQIIII'); ENTRY=struct.Struct('<IIIIQQQIIIIII')
APP_LIMIT=2<<20; FLASH_LIMIT=32<<20; STAGE_LIMIT=16<<20
crc=lambda b:zlib.crc32(b)&0xffffffff
sha=lambda b:hashlib.sha256(b).hexdigest()
def require(ok,message):
    if not ok: raise ValueError(message)
def decoded(data,codec):
    if codec==0:return data
    require(codec==1,'unknown codec')
    d=zlib.decompressobj(-15); out=d.decompress(data); out+=d.flush()
    require(d.eof and not d.unused_data and not d.unconsumed_tail,'DEFLATE exact input violation')
    return out

def validate(blob, expected=None, runtime=True):
    require(len(blob)>=64,'short header')
    magic,ver,hs,es,n,do,po,total,dc,pc,hc,flags=HEADER.unpack_from(blob)
    require((magic,ver,hs,es,do,flags)==(b'ADFLASH3',3,64,64,64,0),'invalid header')
    require(1<=n<=512 and total==len(blob) and total<=FLASH_LIMIT-APP_LIMIT,'size/count')
    require(not runtime or total<=STAGE_LIMIT,'pack exceeds 16MiB runtime stage')
    require(po==((64+n*64+63)//64)*64 and po<=total,'directory/payload bounds')
    h=bytearray(blob[:64]);h[56:60]=b'\0'*4
    require(crc(h)==hc,'headerCRC');require(crc(blob[do:do+n*64])==dc,'directoryCRC')
    require(crc(blob[po:])==pc,'payloadCRC')
    result=[];seen=set();intervals=[]
    for i in range(n):
        kind,id,codec,predictor,off,packed,raw,align,sc,rc,fl,r0,r1=ENTRY.unpack_from(blob,64+i*64)
        require(kind in (1,2,3,4,5,6,7,8,9,10) and codec in (0,1) and predictor in (0,1) and fl==r0==r1==0,'entry enums/reserved')
        require(kind!=8 or (id==0 and codec==1 and predictor==0),'kind8 requires id0 raw DEFLATE predictor0')
        require(kind!=9 or (id in (0,1) and codec==1 and predictor==0),'kind9 requires id0/1 raw DEFLATE predictor0')
        require(kind!=10 or (id==0 and codec==1 and predictor==0 and align==64),'kind10 requires id0 raw DEFLATE predictor0 alignment64')
        require((kind,id) not in seen,'duplicate kind/id');seen.add((kind,id))
        require(align and align<=4096 and not align&(align-1) and off%align==0,'alignment')
        require(off>=po and off<=total and 0<packed<=total-off and 0<raw<=95<<20,'entry bounds')
        require(all(not(off<b and a<off+packed) for a,b in intervals),'overlap');intervals.append((off,off+packed))
        data=blob[off:off+packed];require(crc(data)==sc,'streamCRC');out=decoded(data,codec)
        require(len(out)==raw and crc(out)==rc,'raw size/CRC')
        result.append(dict(kind=kind,id=id,codec=codec,predictor=predictor,offset=off,packed_bytes=packed,raw_bytes=raw,raw_crc32=rc,stream_crc32=sc))
    require([(e['kind'],e['id']) for e in result]==sorted(seen),'directory order')
    if expected is not None:
        actual=[(e['kind'],e['id'],e['codec'],e['predictor'],e['raw_bytes']) for e in result]
        want=sorted((e['kind'],e['id'],e['codec'],e.get('predictor',0),e['raw_bytes']) for e in expected)
        require(actual==want,'consumer manifest mismatch')
    return result

def build(manifest, base):
    items=sorted(manifest['entries'],key=lambda e:(e['kind'],e['id']))
    require(1<=len(items)<=512,'entry count');po=64+64*len(items);payload=bytearray();directory=bytearray()
    for e in items:
        data=(base/e['path']).read_bytes()
        if 'source_offset' in e:
            start=e['source_offset'];size=e['packed_bytes'];require(0<=start<=len(data) and 0<size<=len(data)-start,'source slice bounds');data=data[start:start+size]
        out=decoded(data,e['codec']);alignment=e.get('alignment',64)
        require(alignment and alignment<=4096 and not alignment&(alignment-1),'alignment')
        require(len(out)==e['raw_bytes'],'manifest raw_bytes mismatch')
        if 'raw_crc32' in e: require(crc(out)==e['raw_crc32'],'manifest rawCRC mismatch')
        off=(po+len(payload)+alignment-1)//alignment*alignment
        payload.extend(b'\xff'*(off-po-len(payload)));payload.extend(data)
        directory.extend(ENTRY.pack(e['kind'],e['id'],e['codec'],e.get('predictor',0),off,len(data),len(out),alignment,crc(data),crc(out),0,0,0))
    h=bytearray(HEADER.pack(b'ADFLASH3',3,64,64,len(items),64,po,po+len(payload),crc(directory),crc(payload),0,0));struct.pack_into('<I',h,56,crc(h))
    blob=bytes(h+directory+payload);validate(blob,manifest.get('consumers',items));return blob

def full_image(app,pack):
    require(0<len(app)<=APP_LIMIT,'app size outside 1..2MiB');validate(pack)
    out=app+b'\xff'*(APP_LIMIT-len(app))+pack;require(len(out)<=FLASH_LIMIT,'full flash overflow');return out

def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='cmd',required=True)
    b=sub.add_parser('build');b.add_argument('manifest',type=Path);b.add_argument('output',type=Path)
    v=sub.add_parser('verify');v.add_argument('pack',type=Path);v.add_argument('--manifest',type=Path)
    f=sub.add_parser('full');f.add_argument('app',type=Path);f.add_argument('pack',type=Path);f.add_argument('output',type=Path)
    a=p.parse_args()
    if a.cmd=='build':
        m=json.loads(a.manifest.read_text(encoding='utf-8-sig'));blob=build(m,a.manifest.parent);a.output.write_bytes(blob);print(json.dumps({'bytes':len(blob),'sha256':sha(blob),'entries':validate(blob)},indent=2))
    elif a.cmd=='verify':
        expected=None
        if a.manifest: m=json.loads(a.manifest.read_text(encoding='utf-8-sig'));expected=m.get('consumers',m['entries'])
        print(json.dumps(validate(a.pack.read_bytes(),expected),indent=2))
    else:a.output.write_bytes(full_image(a.app.read_bytes(),a.pack.read_bytes()))
if __name__=='__main__':main()
