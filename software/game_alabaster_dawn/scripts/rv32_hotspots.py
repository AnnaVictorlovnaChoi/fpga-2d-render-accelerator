"""Summarize real RV32 objdump text; static counts are NOT runtime performance."""
import argparse
import collections
import hashlib
import json
import re
from pathlib import Path


def analyze(text):
    functions = {}
    current = None
    helpers = []
    for line in text.splitlines():
        label = re.match(r'^\s*[0-9a-f]+ <([^>]+)>:', line)
        if label:
            current = label[1]
            functions.setdefault(current, collections.Counter())
            continue
        instruction = re.match(r'^\s*([0-9a-f]+):\s+[0-9a-f]+\s+([a-z][a-z0-9.]*)\s*(.*)', line)
        if not instruction or current is None:
            continue
        address, opcode, operands = instruction.groups()
        functions[current]['instructions'] += 1
        if opcode in ('div', 'divu', 'rem', 'remu', 'mul', 'mulh', 'mulhu', 'mulhsu'):
            functions[current][opcode] += 1
        target = re.search(r'<(__[a-z0-9_]*(?:div|mod|mul)[a-z0-9_]*)>', operands)
        if target and opcode in ('jal', 'jalr', 'call', 'j', 'tail'):
            helpers.append(dict(caller=current, address=address, instruction=opcode, helper=target[1], operands=operands))
    return dict(status='STATIC_ONLY', board_test='NOT_PERFORMED', architecture='RV32',
                functions={k:dict(v) for k,v in sorted(functions.items(), key=lambda kv:-kv[1]['instructions'])},
                libgcc_helper_calls_or_tail_jumps=helpers,
                notes=['Counts describe emitted instructions, not dynamic frequency, cycles or timing.',
                       'RV32 div/divu/rem/remu operate on 32-bit registers; __divdi3/__udivdi3 are 64-bit helpers.',
                       'Inspect the retained disassembly and measure the physical CPU; indirect calls may not resolve here.',
                       'Q4 arithmetic transformations still require separate signed-overflow and bounds tests.'])

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('disassembly',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    data=args.disassembly.read_bytes()
    report=analyze(data.decode('utf-8-sig'))
    report['disassembly_sha256']=hashlib.sha256(data).hexdigest()
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
