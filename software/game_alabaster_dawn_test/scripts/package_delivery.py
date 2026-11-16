"""Package current frozen project and ONLY latest verified run; reject stale inputs."""
from pathlib import Path
import hashlib
import json
import zipfile
import verify_build

ROOT = Path(__file__).resolve().parents[1]


def checked_stage(root):
    root = root.resolve()
    text = (root / 'deliverables/latest-success.txt').read_text('utf-8-sig').strip()
    stage = Path(text)
    if not stage.is_absolute(): stage = root / stage
    stage = stage.resolve()
    verify_build.require(stage.parent == root / 'deliverables' and stage.name.startswith('run-'), 'invalid successful stage')
    report = json.loads((stage / 'verification.json').read_text('utf-8'))
    verify_build.require(report.get('status') == 'PASS', 'successful stage is not PASS')
    manifest = json.loads((stage / 'build_manifest.json').read_text('utf-8'))
    verify_build.require(manifest.get('project') == root.name, 'wrong project manifest')
    verify_build.require(manifest['source_inputs'] == verify_build.inventory(root), 'project changed since verified build; rebuild before packaging')
    flavors = manifest.get('flavors', [])
    verify_build.require(len(flavors) == 2 and set(flavors) == {'serial','clean'} and report.get('flavors') == flavors, 'delivery requires verified serial and clean flavors')
    actual = {p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file() and p != stage / 'build_manifest.json'}
    verify_build.require(actual == set(manifest['artifacts']), 'unrecorded or missing run artifact')
    for name, record in manifest['artifacts'].items():
        p = stage / name
        verify_build.require(p.resolve().is_relative_to(stage), 'invalid artifact path')
        verify_build.require(p.stat().st_size == record['bytes'] and verify_build.digest(p) == record['sha256'], 'artifact SHA/size mismatch: ' + name)
    for flavor in flavors:
        sub = stage / flavor
        checked = json.loads((sub / 'verification.json').read_text('utf-8-sig'))
        child = json.loads((sub / 'build_manifest.json').read_text('utf-8-sig'))
        verify_build.require(checked.get('status') == 'PASS' and checked.get('flavor') == flavor and child.get('flavor') == flavor, 'invalid flavor verification')
        verify_build.require(child['source_inputs'] == manifest['source_inputs'], 'flavor source freeze mismatch')
        actual_child = {p.relative_to(sub).as_posix() for p in sub.rglob('*') if p.is_file() and p != sub / 'build_manifest.json'}
        verify_build.require(actual_child == set(child['artifacts']), 'unrecorded or missing flavor artifact')
        for name, record in child['artifacts'].items():
            p = sub / name
            verify_build.require(p.resolve().is_relative_to(sub) and p.stat().st_size == record['bytes'] and verify_build.digest(p) == record['sha256'], 'flavor artifact SHA/size mismatch: ' + name)
        verify_build.require((sub / 'assets.pack').read_bytes() == (stage / 'assets.pack').read_bytes(), 'shared pack mismatch')
        verify_build.require((stage / ('G2.6_' + flavor + '.full.flash.hex')).read_bytes() == (sub / 'full.flash.hex').read_bytes(), 'root HEX mismatch')
    verify_build.require(not list(stage.rglob('app.hex')), 'app-only HEX forbidden')
    verify_build.require(set(stage.glob('*.hex')) == {stage / ('G2.6_' + f + '.full.flash.hex') for f in flavors}, 'unexpected root HEX')
    return stage


def archive_members(root, stage):
    # Source freeze is the explicit project input allowlist, never arbitrary
    # root files (which can contain copied older HEX/ZIP delivery products).
    members = [root / name for name in verify_build.inventory(root)]
    members += [p for p in stage.rglob('*') if p.is_file()]
    return sorted(set(members))


def main():
    stage = checked_stage(ROOT)
    members = archive_members(ROOT, stage)
    out = ROOT / ('deliverables/G2.6_300MHz_verified_project_' + stage.name + '.zip')
    if out.exists(): raise ValueError('package exists; refusing overwrite')
    prefix = ROOT.name + '/'
    hashes = {prefix + p.relative_to(ROOT).as_posix(): verify_build.digest(p) for p in members}
    # Portable pointer inside ZIP: no host-absolute drive dependency.
    pointer = prefix + 'deliverables/latest-success.txt'
    pointer_bytes = ('deliverables/' + stage.name + '\n').encode()
    hashes[pointer] = hashlib.sha256(pointer_bytes).hexdigest()
    with zipfile.ZipFile(out, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in members: z.write(p, prefix + p.relative_to(ROOT).as_posix())
        z.writestr(pointer, pointer_bytes)
    with zipfile.ZipFile(out) as z:
        if z.testzip() is not None: raise ValueError('ZIP CRC failure')
        if len(z.namelist()) != len(hashes) or set(z.namelist()) != set(hashes): raise ValueError('ZIP member mismatch')
        for name, expected in hashes.items():
            if hashlib.sha256(z.read(name)).hexdigest() != expected: raise ValueError('ZIP SHA mismatch: ' + name)
    checked_stage(ROOT)  # Detect concurrent source/artifact changes before declaring PASS.
    result = dict(status='PASS', zip=str(out), members=len(hashes), bytes=out.stat().st_size,
                  sha256=verify_build.digest(out), successful_run=str(stage), verified_each_member=True,
                  board_validation=False, cpu_budget_measured=False)
    verify_build.save(ROOT / 'deliverables/package_verification.json', result)
    print(json.dumps(result))


if __name__ == '__main__': main()
