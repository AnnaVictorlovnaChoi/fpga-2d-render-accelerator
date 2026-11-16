"""Strict static build gates; no hardware execution or dynamic-MMIO proof is claimed."""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from datetime import datetime, timezone
from pathlib import Path
import pack_flash

APP_BASE = 0x1000
APP_LIMIT = 2 << 20
STAGE_START, STAGE_END = 0x04F01000, 0x05F01000
FRAME_START = 0x06000000


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    return h.hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def inventory(root):
    paths = [root / 'build.ps1', root / 'README.md']
    for name in ('src', 'platform', 'scripts', 'resources', 'tests', 'docs'):
        paths += [p for p in (root / name).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.pyc']
    return {p.relative_to(root).as_posix(): digest(p) for p in sorted(set(paths)) if p.is_file()}


def freeze(root, output, compare=None):
    current = inventory(root)
    save(output, current)
    if compare:
        require(current == json.loads(compare.read_text(encoding='utf-8-sig')), 'source/input hashes changed during build')
    return current


def elf_check(path, app):
    data = path.read_bytes()
    require(len(data) >= 52 and data[:7] == b'\x7fELF\x01\x01\x01', 'not ELF32 little-endian v1')
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    _, typ, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, shstr = h
    require((typ, machine, version, entry, ehsize, phsize, shsize) == (2, 243, 1, APP_BASE, 52, 32, 40), 'ELF type/RISCV/entry/header mismatch')
    require(flags & 0xF == 0, 'ELF must use soft-float ILP32, no RVC/RVE/TSO flags')
    require(phnum and shnum and shstr < shnum, 'missing ELF tables')
    require(phoff + phnum * phsize <= len(data) and shoff + shnum * shsize <= len(data), 'ELF table bounds')
    loads = []
    for i in range(phnum):
        pt, off, va, pa, fs, ms, pf, align = struct.unpack_from('<8I', data, phoff + i * phsize)
        if pt != 1:
            continue
        require(fs <= ms and off + fs <= len(data), 'PT_LOAD file bounds')
        require(va == pa and APP_BASE <= va < va + ms <= STAGE_START, 'PT_LOAD overlaps stage/frame or nonidentity address')
        require(not align or (not align & (align - 1) and (va - off) % align == 0), 'PT_LOAD alignment')
        loads.append(dict(offset=off, vaddr=va, filesz=fs, memsz=ms, flags=pf))
    require(loads and min(p['vaddr'] for p in loads) == APP_BASE, 'load base must equal boot copy base')
    require(any(p['flags'] & 1 and p['vaddr'] <= entry < p['vaddr'] + p['filesz'] for p in loads), 'entry not executable file data')
    for i, p in enumerate(loads):
        require(all(not (p['vaddr'] < q['vaddr'] + q['memsz'] and q['vaddr'] < p['vaddr'] + p['memsz']) for q in loads[:i]), 'overlapping PT_LOAD')
    sections = [struct.unpack_from('<10I', data, shoff + i * shsize) for i in range(shnum)]
    names = sections[shstr]
    require(names[4] + names[5] <= len(data), 'section names bounds')
    strings = data[names[4]:names[4] + names[5]]
    def name(offset):
        require(offset < len(strings), 'invalid section name offset')
        return strings[offset:].split(b'\0', 1)[0].decode('ascii')
    named = {name(s[0]): s for s in sections}
    for s in sections:
        if s[1] != 8:
            require(s[4] + s[5] <= len(data), 'section file bounds')
        if s[2] & 2 and s[5]:
            require(APP_BASE <= s[3] < s[3] + s[5] <= STAGE_START, 'allocated section outside application RAM')
            require(any(p['vaddr'] <= s[3] and s[3] + s[5] <= p['vaddr'] + p['memsz'] for p in loads), 'allocated section not in PT_LOAD')
    require('.bss' in named and '.stack' in named, 'missing BSS/stack')
    bss, stack = named['.bss'], named['.stack']
    require(bss[1] == stack[1] == 8 and stack[5] == 4096 and stack[3] % 16 == 0, 'BSS/stack must be NOLOAD; stack exactly aligned 4096 bytes')
    require(bss[3] + bss[5] <= stack[3] and stack[3] + stack[5] <= STAGE_START < STAGE_END < FRAME_START, 'BSS/stack/stage/frame overlap')
    expected_end = max(p['vaddr'] + p['filesz'] for p in loads if p['filesz'])
    require(0 < len(app) <= APP_LIMIT and len(app) == expected_end - APP_BASE, 'app BIN length does not match ELF loaded file span')
    for p in loads:
        start = p['vaddr'] - APP_BASE
        require(app[start:start + p['filesz']] == data[p['offset']:p['offset'] + p['filesz']], 'ELF/BIN PT_LOAD mismatch')
    # Check all gaps too: objcopy binary represents absent file bytes as zero.
    image = bytearray(len(app))
    for p in loads:
        start = p['vaddr'] - APP_BASE
        image[start:start + p['filesz']] = data[p['offset']:p['offset'] + p['filesz']]
    require(app == image, 'ELF/BIN gap mismatch')
    return dict(entry=entry, machine=machine, loads=loads, bss=[bss[3], bss[3] + bss[5]], stack=[stack[3], stack[3] + stack[5]], external_stage=[STAGE_START, STAGE_END], framebuffers=[[a, a + 1280 * 720 * 2] for a in (0x06000000, 0x06200000, 0x06400000)])


def hex_check(hex_path, binary):
    pos = 0
    with hex_path.open('rb') as f:
        for line in f:
            require(re.fullmatch(rb'[0-9A-Fa-f]{2}\r?\n', line) is not None, 'invalid byte-per-line HEX')
            require(pos < len(binary) and int(line.strip(), 16) == binary[pos], 'HEX roundtrip mismatch')
            pos += 1
    require(pos == len(binary), 'HEX line count mismatch')
    return pos


def render_check(source, header):
    """Conservative explicit-opcode source audit, not arbitrary C control-flow proof."""
    clean = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    for name, number in [('COPY', 0), ('COLOR', 1), ('SPRITE', 2), ('ALPHA', 3), ('ALPHASPRITE', 5)]:
        require(re.search(r'#define\s+GPU_OPERATION_' + name + r'\s+' + str(number) + r'u?\b', header), 'GPU opcode mismatch: ' + name)
    require(not re.search(r'GPU_OPERATION_ZOOM\b|\bpic_zoom\s*\(', clean), 'unsupported render GPU operation')
    functions = []
    for match in re.finditer(r'\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{', clean):
        if match[1] in ('if', 'for', 'while', 'switch'): continue
        depth, pos = 1, match.end()
        while depth and pos < len(clean):
            depth += (clean[pos] == '{') - (clean[pos] == '}'); pos += 1
        functions.append((match[1], clean[match.end():pos-1]))
    require(functions and 'GPU_OPERATION_COPY' in clean and 'GPU_OPERATION_COLOR' in clean, 'render must explicitly use COPY0 and COLOR1')
    effects, grey, alpha, pieces = [], [], [], []
    piece_names = {'sprite_piece', 'rect_piece'}
    # Bound Q5 values selected by piece callers; this remains a source convention,
    # not general data-flow proof. Coordinates and index constants are unrelated.
    for name, body in functions:
        if not re.search(r'\b(?:sprite_piece|rect_piece)\s*\(', body):
            continue
        for value in re.findall(r'\bq\s*=(?!=)\s*([^;]+);', body):
            choices = value.rsplit('?', 1)[-1] if '?' in value else value
            require(re.fullmatch(r'\s*(?:16|26|32)u?\s*(?::\s*(?:16|26|32)u?\s*)?', choices),
                    'unsupported piece alpha Q5 assignment in ' + name)
        for args in re.findall(r'\b(?:sprite_piece|rect_piece)\s*\(([^;{}]*)\)\s*;', body):
            weight = args.rsplit(',', 1)[-1].strip()
            require(weight == 'q' or re.fullmatch(r'(?:16|26|32)u?', weight),
                    'unsupported piece alpha Q5 call in ' + name)
    require(not re.search(r'\b(?:asset_pixels|framebuffer|pixels)\s*\[[^]]+\]\s*=(?!=)|\b(?:memcpy|memmove)\s*\(', clean), 'CPU pixel write/copy in renderer')
    for name, body in functions:
        if re.search(r'GPU_OPERATION_ALPHA\b|\bpic_alpha\s*\(', body):
            if name in piece_names:
                require(re.search(r'q\s*==\s*32u?\s*\?\s*GPU_OPERATION_COPY\s*:\s*GPU_OPERATION_ALPHA\b', body),
                        'piece ALPHA3 must map opaque Q5=32 to COPY0: ' + name)
                pieces.append(name)
            else:
                require(any(word in name.lower() for word in ('gray', 'grey', 'wash', 'defeat')), 'ALPHA3 outside dedicated graywash/piece function: ' + name)
                grey.append(name)
        if re.search(r'GPU_OPERATION_SPRITE\b|\bpic_sprite\s*\(', body):
            require('effect' in name.lower() or name in ('cache_sprite', 'sprite_piece'), 'SPRITE2 outside six-cache/effects function: ' + name)
            effects.append(name)
        if re.search(r'GPU_OPERATION_ALPHASPRITE\b|\bpic_alphasprite\s*\(', body):
            require(name in ('cache_sprite', 'sprite_piece', 'split_sprite'), 'ALPHASPRITE5 outside cached alpha helper: ' + name)
            alpha.append(name)
    pattern = r'GPU_OPERATION_(?:SPRITE|ALPHA|ALPHASPRITE)\b|\bpic_(?:sprite|alpha|alphasprite)\s*\('
    require(len(re.findall(pattern, clean)) == sum(len(re.findall(pattern, b)) for _, b in functions), 'unscoped SPRITE use')
    require(not re.search(r'\bbitblt\s*\(\s*(?:[2-9]|0x[2-9a-f])\b', clean), 'literal non COPY/COLOR opcode bypass')
    if alpha:
        require(re.search(r'(?:alpha|\([^;{}]+\))\s*\?\s*16u?\s*:\s*32u?', clean), 'cached alpha split must use Q5=16, opaque=32')
        for name, body in functions:
            if name in piece_names and re.search(r'GPU_OPERATION_ALPHASPRITE\b', body):
                require(re.search(r'q\s*==\s*32u?\s*\?\s*GPU_OPERATION_SPRITE\s*:\s*GPU_OPERATION_ALPHASPRITE\b', body),
                        'piece ALPHASPRITE5 must map opaque Q5=32 to SPRITE2: ' + name)
    return dict(cached_alpha_functions=alpha, alpha_q5=16, piece_alpha_q5=[16, 26], opaque_q5=32,
                piece_alpha_functions=pieces, normal_operations={'COPY': 0, 'COLOR': 1}, effects_only_sprite_functions=effects, defeat_only_alpha_functions=grey,
                scope='Explicit source-opcode audit only; not dynamic command execution or performance proof.')


def preflight(root):
    manifest = json.loads((root / 'resources/external_assets.manifest.json').read_text(encoding='utf-8-sig'))
    entries = pack_flash.validate((root / 'resources/assets.pack').read_bytes(), manifest.get('consumers', manifest['entries']))
    require(sum(e['raw_bytes'] for e in entries) < STAGE_START - APP_BASE, 'decoded resource bytes exhaust low79MiB before other BSS/stack')
    for p in (root / 'src').rglob('*'):
        if p.is_file() and p.suffix.lower() in ('.c', '.h', '.s'):
            require(not re.search(r'\.incbin\b', p.read_text(errors='replace')), 'external assets must not use incbin: ' + str(p))
    return render_check((root / 'src/render.c').read_text(encoding='utf-8-sig'), (root / 'platform/gpu/2D_DMA.h').read_text(encoding='utf-8-sig'))


def tool_output(tool, args, output):
    p = subprocess.run([str(tool), *map(str, args)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    output.write_bytes(p.stdout)
    require(p.returncode == 0, f'{tool.name} failed: {p.stderr.decode(errors="replace")}')
    return p.stdout.decode(errors='replace').replace('\r\n', '\n')


def uart_tx_stores(dis, address=0xf8010000):
    """Known-address RISC-V constant propagation; not general indirect-MMIO proof."""
    regs, stores = {'zero': 0}, []
    for line in dis.splitlines():
        if re.match(r'^\s*[0-9a-f]+ <[^>]+>:', line):
            regs = {'zero': 0}
            continue
        m = re.match(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2,8}\s+)+([a-z][a-z0-9.]*)\s*(.*)', line)
        if not m: continue
        pc, op, args = m.groups()
        args = args.split('#')[0].strip()
        parts = [v.strip() for v in args.split(',')]
        def imm(v):
            return int(v, 16) if v.lower().startswith(('0x','-0x')) else int(v)
        if op in ('sw', 'sh', 'sb') and len(parts) == 2:
            mem = re.fullmatch(r'(-?(?:0x[0-9a-f]+|[0-9]+))\((\w+)\)', parts[1])
            if mem and mem[2] in regs:
                target = (regs[mem[2]] + imm(mem[1])) & 0xffffffff
                if target <= address < target + {'sw':4,'sh':2,'sb':1}[op]: stores.append(pc)
            continue
        if op in ('beq','bne','blt','bge','bltu','bgeu','beqz','bnez','j','ret','fence','fence.i','nop'): continue
        if not parts: continue
        dst = parts[0]
        value = None
        try:
            if op == 'lui': value = int(parts[1], 16) << 12
            elif op == 'li': value = imm(parts[1])
            elif op == 'mv': value = regs.get(parts[1])
            elif op == 'addi' and parts[1] in regs: value = regs[parts[1]] + imm(parts[2])
        except (ValueError, IndexError): pass
        regs.pop(dst, None)
        if value is not None: regs[dst] = value & 0xffffffff
        regs['zero'] = 0
    return stores


def serial_check(nm, dis, flavor):
    require(flavor in ('serial', 'clean'), 'unknown flavor')
    pattern = r'\b(?:bsp_printf|uart_write)(?:[_.$]\w+)*\b'
    present = sorted(set(re.findall(pattern, nm + '\n' + dis)))
    stores = uart_tx_stores(dis)
    if flavor == 'clean':
        require(not present and not stores, 'clean UART output symbol/TXDATA store gate failed: ' + ', '.join(present + stores))
    else:
        require(re.search(r'\bbsp_printf(?:[.$]\w+)*\b', nm) and
                re.search(r'<bsp_printf(?:[.][^>]*)?>', dis), 'diagnostic bsp_printf missing from final ELF')
        require(stores, 'diagnostic final disassembly has no known UART TXDATA store')
    return dict(flavor=flavor, g24_serial=int(flavor == 'serial'), output_symbols=present,
                uart_data_address=0xf8010000, known_tx_store_pcs=stores,
                scope='Final ELF nm/disassembly; uart_write may inline. Known-address constant propagation is not arbitrary indirect-MMIO proof. UART configuration permitted; no hardware measurement.')


def verify(root, stage, toolchain, flavor):
    host = json.loads((stage / 'host_tests.json').read_text(encoding='utf-8-sig'))
    mandatory = {'tests/test_game.py', 'tests/test_pack.py', 'tests/test_render.py', 'tests/test_real_game.py', 'tests/test_printf_regression.py', 'tests/test_gpu_cache.py', 'tests/test_assets_cache.py', 'tests/test_clock_300mhz.py', 'tests/test_fire_schedule.py', 'tests/test_alpha_tags.py', 'tests/test_perf_assets.py', 'tests/test_alpha_split.py', 'tests/test_main_metrics.py', 'tests/test_triple_pipeline.py','tests/test_speed_1p2.py'}
    require(host.get('status') == 'PASS' and mandatory.issubset(set(host.get('tests', []))), 'mandatory host tests not passed')
    app = (stage / 'app.bin').read_bytes()
    pack = (stage / 'assets.pack').read_bytes()
    manifest = json.loads((root / 'resources/external_assets.manifest.json').read_text(encoding='utf-8-sig'))
    entries = pack_flash.validate(pack, manifest.get('consumers', manifest['entries']))
    require(pack == (root / 'resources/assets.pack').read_bytes(), 'stage/source assets.pack mismatch')
    full = (stage / 'full.flash.bin').read_bytes()
    require(full == pack_flash.full_image(app, pack), 'full image must equal app + FF padding to 2MiB + pack exactly')
    result = {'elf': elf_check(stage / 'app.elf', app), 'pack_entries': entries,
              'hex_bytes': {'app': hex_check(stage / 'app.hexcheck', app), 'full.flash': hex_check(stage / 'full.flash.hex', full)}}
    suffix = '.exe' if (toolchain / 'riscv-none-elf-nm.exe').exists() else ''
    nm = tool_output(toolchain / ('riscv-none-elf-nm' + suffix), ['-n', stage / 'app.elf'], stage / 'nm.txt')
    require(re.search(r'^0*1000\s+\w\s+_start$', nm, re.M), '_start must equal 0x1000')
    require(re.search(r'^\w+\s+[Tt]\s+trap$', nm, re.M), 'main-provided trap function missing')
    symbols = {m[3]: int(m[1], 16) for m in re.finditer(r'^([0-9a-fA-F]+)\s+(\w)\s+(\S+)$', nm, re.M)}
    for name, value in [('__external_stage_start', STAGE_START), ('__external_stage_end', STAGE_END), ('__stack_size', 4096), ('_sp', result['elf']['stack'][1])]:
        require(symbols.get(name) == value, 'fixed linker symbol mismatch: ' + name)
    map_text = (stage / 'app.map').read_text(errors='replace')
    require(all(n in map_text for n in ('.stack', '__external_stage_start', '__external_stage_end', '2D_DMA.c.o')), 'map missing linker/driver evidence')
    dis = tool_output(toolchain / ('riscv-none-elf-objdump' + suffix), ['-d', stage / 'app.elf'], stage / 'objdump.txt')
    soc = (root / 'platform/bsp/include/soc.h').read_text()
    uart = (root / 'platform/driver/uart.h').read_text()
    require(re.search(r'#define\s+SYSTEM_UART_0_IO_CTRL\s+0xf8010000\b', soc, re.I) and re.search(r'#define\s+UART_DATA\s+0x00\b', uart), 'UART TXDATA address changed; update final disassembly audit')
    result['serial_static'] = serial_check(nm, dis, flavor)
    result['flavor'] = flavor
    result['render_static'] = preflight(root)
    frames = []
    for p in sorted(stage.rglob('*.su')):
        for line in p.read_text(errors='replace').splitlines():
            fields = line.split('\t')
            require(len(fields) == 3 and fields[1].isdigit(), 'unrecognized stack usage record: ' + line)
            frames.append({'location': fields[0], 'bytes': int(fields[1]), 'kind': fields[2]})
    require(frames, 'missing compiler stack usage reports')
    require(all(f['bytes'] <= 4096 and f['kind'] in ('static', 'dynamic,bounded') for f in frames), 'unbounded or oversized single function stack frame')
    result['stack_static'] = {'frames': frames, 'max_single_frame': max(f['bytes'] for f in frames),
        'all_frame_sum': sum(f['bytes'] for f in frames),
        'scope': 'Compiler per-function static records only, not a call-chain bound or board high-water mark. No old-project call graph reused. Recursion, indirect calls, ISR nesting and prebuilt library stack usage remain unbounded by this report.'}
    freeze(root, stage / 'source_hashes.after.json', stage / 'source_hashes.before.json')
    result.update(status='PASS', verified_at=datetime.now(timezone.utc).isoformat())
    save(stage / 'verification.json', result)
    products = ['host_tests.json', 'nm.txt', 'objdump.txt', 'full.flash.hex', 'full.flash.bin', 'app.elf', 'app.bin', 'app.map', 'app.hexcheck', 'assets.pack', 'verification.json', 'source_hashes.before.json', 'source_hashes.after.json']
    products += [p.relative_to(stage).as_posix() for p in sorted((stage / 'objects').rglob('*')) if p.is_file()]
    build_manifest = {'project': root.name, 'built_at': result['verified_at'], 'architecture': 'rv32im_zicsr_zifencei', 'abi': 'ilp32', 'optimization': 'O2', 'lto': False, 'flavor': flavor, 'g24_serial': int(flavor == 'serial'), 'platform': 'V10_300MHz_G2.6', 'clint_hz': 300000000, 'cpu_budget_ticks': 600000, 'hardware_cache_user_declared': {'dcache_kib': 16, 'icache_kib': 8}, 'toolchain_bin': str(toolchain),
        'app_base': APP_BASE, 'app_limit': APP_LIMIT, 'pack_flash_offset': APP_LIMIT, 'flash_limit': 32 << 20,
        'source_inputs': json.loads((stage / 'source_hashes.before.json').read_text()),
        'artifacts': {n: {'bytes': (stage / n).stat().st_size, 'sha256': digest(stage / n)} for n in products}}
    save(stage / 'build_manifest.json', build_manifest)
    return result


def finish(root, stage, flavors):
    require(flavors and len(flavors) == len(set(flavors)) and set(flavors) <= {'serial', 'clean'}, 'invalid flavors')
    source = json.loads((stage / 'source_hashes.before.json').read_text('utf-8-sig'))
    require(source == inventory(root), 'source changed before run completion')
    pack = (stage / 'assets.pack').read_bytes()
    for flavor in flavors:
        sub = stage / flavor
        report = json.loads((sub / 'verification.json').read_text('utf-8-sig'))
        manifest = json.loads((sub / 'build_manifest.json').read_text('utf-8-sig'))
        require(report.get('status') == 'PASS' and report.get('flavor') == flavor and manifest.get('flavor') == flavor, 'flavor not verified')
        require(manifest['source_inputs'] == source, 'flavor source freeze differs')
        require((sub / 'assets.pack').read_bytes() == pack, 'flavors must share exact resource pack')
        for name, record in manifest['artifacts'].items():
            path = sub / name
            require(path.resolve().is_relative_to(sub.resolve()) and path.stat().st_size == record['bytes'] and digest(path) == record['sha256'], 'flavor artifact changed: ' + name)
        require((stage / ('G2.6_' + flavor + '.full.flash.hex')).read_bytes() == (sub / 'full.flash.hex').read_bytes(), 'root HEX differs from verified flavor')
    require(not list(stage.rglob('app.hex')), 'app-only HEX forbidden')
    expected_hex = {stage / ('G2.6_' + f + '.full.flash.hex') for f in flavors}
    require(set(stage.glob('*.hex')) == expected_hex, 'unexpected root HEX')
    save(stage / 'verification.json', dict(status='PASS', flavors=flavors, shared_pack_sha256=digest(stage / 'assets.pack')))
    products = [p for p in sorted(stage.rglob('*')) if p.is_file() and p != stage / 'build_manifest.json']
    save(stage / 'build_manifest.json', dict(project=root.name, flavors=flavors, source_inputs=source,
         artifacts={p.relative_to(stage).as_posix(): dict(bytes=p.stat().st_size, sha256=digest(p)) for p in products}))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    sub = p.add_subparsers(dest='cmd', required=True)
    f = sub.add_parser('freeze'); f.add_argument('output', type=Path); f.add_argument('--compare', type=Path)
    v = sub.add_parser('verify'); v.add_argument('stage', type=Path); v.add_argument('--toolchain-bin', type=Path, required=True); v.add_argument('--flavor', choices=('serial','clean'), required=True)
    f = sub.add_parser('finish'); f.add_argument('stage', type=Path); f.add_argument('--flavors', nargs='+', choices=('serial','clean'), required=True)
    sub.add_parser('preflight')
    a = p.parse_args()
    try:
        if a.cmd == 'freeze': freeze(a.root, a.output, a.compare)
        elif a.cmd == 'preflight': preflight(a.root)
        elif a.cmd == 'finish': finish(a.root, a.stage, a.flavors)
        else: verify(a.root, a.stage, a.toolchain_bin, a.flavor)
    except Exception as exc:
        if a.cmd in ('verify', 'finish'): save(a.stage / 'verification.json', {'status': 'FAIL', 'error': str(exc)})
        raise
    print('PASS: ' + a.cmd)


if __name__ == '__main__':
    main()
