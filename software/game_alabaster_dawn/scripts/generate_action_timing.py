"""G222 metadata-only rational timing replay from immutable G221 snapshots.
Never reads a generated header as input. Never opens pixel payloads for writing.
"""
from pathlib import Path
import hashlib
import re
import generate_render_tables

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {
    'g221_fpga_player.h': 'b25c68f7db885632f920546952aa54c9839e87b70d824841bf374b03a4ccda5c',
    'g221_arrow_data.c': 'cc7d2e6cb57b82a435a9c4617e09538e0acb71910b38369fbf6f1e1c513d24ec',
}

def source(name):
    data = (ROOT / 'resources/timing_source' / name).read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCES[name]:
        raise ValueError('Frozen G221 source hash mismatch: ' + name)
    return data.decode('utf-8')

def generate():
    text = source('g221_fpga_player.h')
    def group(m):
        num, den = (10, 13) if m[2] == 'melee' else (2, 3)
        previous = 0
        def frame(f):
            nonlocal previous
            row = list(map(int, f[1].split(',')))
            row[-2] = previous
            row[-1] = max(previous + 1, row[-1] * num // den)
            previous = row[-1]
            return '{' + ','.join(map(str, row)) + '}'
        return m[1] + re.sub(r'\{([^{}]+)\}', frame, m[3]) + m[4]
    text, count = re.subn(r'(static const FpgaPlayerFrame fpga_(melee|ranged)_\w+_frames\[\] = \{)(.*?)(\n\};)', group, text, flags=re.S)
    assert count == 16
    (ROOT / 'resources/player/fpga_player.h').write_text(text, encoding='utf-8', newline='\n')
    arrows = source('g221_arrow_data.c')
    prefix, events = arrows.split('const ArrowEvent arrow_events[8][15]=', 1)
    body, suffix = events.split('\n};', 1)
    body, count = re.subn(r'\{(\d+),(\d+),(-?\d+),(-?\d+)\}', lambda m: '{' + str(int(m[1])*2//3) + ',' + ','.join(m.group(i) for i in (2,3,4)) + '}', body)
    assert count == 120
    (ROOT / 'src/arrow_data.c').write_text(prefix + 'const ArrowEvent arrow_events[8][15]=' + body + '\n};' + suffix, encoding='utf-8', newline='\n')
    generate_render_tables.generate()
    print('G222 replay: melee 10/13; ranged and 120 fires 2/3; no pixel or velocity changes')

if __name__ == '__main__':
    generate()
