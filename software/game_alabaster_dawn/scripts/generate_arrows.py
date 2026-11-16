from pathlib import Path
import json,math
R=Path(__file__).resolve().parents[1];D=R/'resources/arrows';PC=D
m=json.loads((D/'manifest.json').read_text());p=json.loads((D/'pc_parameters.json').read_text());fan=m['fan_angles']
# Pack exact accepted compressed streams and metadata.
blob=bytearray();frames=[];raw_off=0
for f in fan:
 b=(PC/f['deflate']).read_bytes();w,hh=f['size'];ax,ay=f['anchor_shaft_center_q8']
 frames.append((len(blob),len(b),raw_off,w,hh,ax,ay));blob+=b;raw_off+=w*hh*2
(D/'arrows.pack').write_bytes(blob)
lines=['#include "arrow_data.h"','const ArrowAsset arrow_assets[104]={']
lines += [' {'+','.join(map(str,f))+'},' for f in frames];lines+=['};','const ArrowEvent arrow_events[8][15]={']
for name,d in p['directions'].items():
 lines.append(' {')
 for e in d['events']:
  angle=e['angle_clockwise_deg'];sid=next(i for i,f in enumerate(fan) if abs(f['angle_clockwise_deg']-e['baked_angle_deg'])<0.00001)
  a=math.radians(angle);lines.append(' {'+','.join(map(str,[e['fire_us'],sid,round(math.cos(a)*360*65536),round(math.sin(a)*360*65536)]))+'},')
 lines.append(' },')
lines+=['};','const int32_t arrow_origin_q8[8][2]={']
for i in range(8):
 a=-i*math.pi/4;lines.append('{%d,%d},'%(round(math.cos(a)*45*256),round((-55+math.sin(a)*45)*256)))
lines+=['};'];(R/'src/arrow_data.c').write_text('\n'.join(lines))
(R/'src/arrow_data.h').write_text('''#ifndef ARROW_DATA_H
#define ARROW_DATA_H
#include <stdint.h>
typedef struct {uint32_t offset,compressed,raw_offset; int32_t w,h,ax,ay;} ArrowAsset;
typedef struct {uint32_t fire_us,sprite;int32_t vx_q16,vy_q16;} ArrowEvent;
extern const ArrowAsset arrow_assets[104];
extern const ArrowEvent arrow_events[8][15];
extern const int32_t arrow_origin_q8[8][2];
extern const unsigned char arrow_pack[];
#define ARROW_RAW_BYTES %d
#endif
'''%raw_off)
(R/'src/arrow_blob.S').write_text('.section .rodata\n.balign 64\n.global arrow_pack\narrow_pack:\n.incbin "resources/arrows/arrows.pack"\n')
print('arrow compressed',len(blob),'raw',raw_off)
