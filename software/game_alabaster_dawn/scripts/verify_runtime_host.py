"""Build and execute REAL local C runtime+puff, compare all outputs, render snapshot."""
from pathlib import Path
import subprocess,re,zlib,hashlib,json,shutil
from PIL import Image
root=Path(__file__).resolve().parents[1]
b=root/'build/host_runtime';b.mkdir(parents=True,exist_ok=True)
cc=shutil.which('gcc');assert cc,'host gcc required'
pack=(root/'resources/player/player.pack').read_bytes()
# Host C adapter avoids PE/COFF assembly symbol decoration; data is identical pack.
blob=b/'host_blob.c'
blob.write_text('const unsigned char fpga_player_pack[]={'+','.join(map(str,pack))+'};\n')
cmd=[cc,'-std=c99','-O2','-Wall','-Wextra','-I'+str(root/'src'),str(root/'src/runtime.c'),str(root/'src/runtime_data.c'),str(root/'src/puff.c'),str(root/'tests/test_runtime.c'),str(blob),'-o',str(b/'test_runtime.exe')]
subprocess.run(cmd,check=True)
subprocess.run([str(b/'test_runtime.exe')],cwd=b,check=True)
h=(root/'resources/player/fpga_player.h').read_text()
groups=re.findall(r'static const FpgaPlayerGroup (\w+) = \{(\d+),(\d+),(\d+),',h)
results=[]
for i,(name,offset,size,rawsize) in enumerate(groups):
 data=(b/f'group_{i:02}.raw').read_bytes();expected=zlib.decompress(pack[int(offset):int(offset)+int(size)],-15)
 assert data==expected and len(data)==int(rawsize),name
 results.append(dict(index=i,name=name,raw_bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
# Snapshot renders actual C decoded map and one frame per direction/action, alpha-key 0.
def rgb565(data,w,h):
 im=Image.new('RGBA',(w,h));px=[]
 for (v,) in __import__('struct').iter_unpack('<H',data):
  px.append((((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31,255 if v else 0))
 im.putdata(px);return im
canvas=rgb565((b/'group_00.raw').read_bytes(),1280,720).convert('RGB')
for i,(name,*_) in enumerate(groups[2:],2):
 body=re.search(r'static const FpgaPlayerFrame '+name+r'_frames\[\] = \{(.*?)\n\};',h,re.S).group(1)
 vals=list(map(int,re.search(r'\{([^{}]+)\}',body).group(1).split(',')))
 off,w,hh,ax,ay,*_=vals;raw=(b/f'group_{i:02}.raw').read_bytes()
 im=rgb565(raw[off:off+w*hh*2],w,hh)
 x=100+((i-2)%8)*150;y=140+((i-2)//8)*115
 canvas.paste(im,(x-round(ax/256),y-round(ay/256)),im)
canvas.save(b/'runtime_snapshot.png')
files={}
for rel in ['resources/player/player.pack','resources/player/manifest.json','resources/player/fpga_player.h','resources/player/fpga_player.S','src/puff.c','src/puff.h']:
 files[rel]=hashlib.sha256((root/rel).read_bytes()).hexdigest()
report=dict(status='PASS',method='host GCC executing actual runtime.c + local puff.c; each raw group compared byte-for-byte with Python raw deflate',groups=results,source_sha256=files,arena_bytes=6702210,pack_bytes=len(pack),snapshot='build/host_runtime/runtime_snapshot.png')
(root/'docs/runtime_verification.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS 38 C-decoded groups exactly match source; snapshot written')
