"""PC yellow orb geometry at 1.2x speed; same 12s explosion/shot cadence."""
from pathlib import Path
import math
ROOT=Path(__file__).resolve().parents[1]
rows=[]
for i in range(30):
 t=i/29;rows.append((round(64+(640-64)*t),round(315+(380-315)*t-math.sin(math.pi*t)*24)))
text='/* G2.6: path identical; duration1000ms *5/6, cadence unchanged. */\n#ifndef G21_ORB_PATH_H\n#define G21_ORB_PATH_H\n#include <stdint.h>\n#define YELLOW_ORB_DURATION_MS 833u\n#define YELLOW_ORB_START_MS 11167u\nstatic const int16_t yellow_orb_path[30][2]={\n'+',\n'.join('{%d,%d}'%r for r in rows)+'\n};\n#endif\n'
(ROOT/'src/orb_path.h').write_text(text,encoding='utf-8')
