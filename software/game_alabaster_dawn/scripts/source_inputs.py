"""One canonical frozen-input inventory for build, finalize and archive gates."""
from pathlib import Path
import hashlib, json, sys

GENERATED_REPORTS = {'DELIVERY_VERIFICATION.json', 'ARCHIVE_VERIFICATION.json'}
GENERATED_TEST_DIRS = {'build', 'gpu_build', 'projectile_build', 'generated', 'tmpcopy', 'tmp_copy', 'fake_model_outputs', 'model_outputs'}
FIXTURE_DIRS = {'fixtures', 'golden', 'baseline', 'baselinefixtures', 'baseline_fixtures'}

def included(rel):
    """Exclude generated test binaries, never fixtures/golden or agent reports."""
    if rel.parts[0]=='docs' and ((rel.suffix == '.json' and rel.name.startswith('test_') and not rel.name.startswith('test_g225_')) or 'precompute_evidence' in rel.parts or 'speed_evidence' in rel.parts):
        return False  # inherited proof, not current G225 release evidence
    if rel.name in GENERATED_REPORTS or '__pycache__' in rel.parts:
        return False
    if rel.parts[0] in ('tests', 'newtests'):
        fixture = any(part in FIXTURE_DIRS for part in rel.parts[1:-1])
        if not fixture and any(part in GENERATED_TEST_DIRS or part.endswith('_build') for part in rel.parts[1:-1]):
            return False
        if not fixture and rel.suffix.lower() in ('.exe', '.dll', '.o', '.obj', '.raw', '.elf', '.bin', '.hex', '.map', '.su'):
            return False
        if rel.parent.name == 'gpu_scene_assets' and (rel.suffix == '.S' or rel.name == 'bad.pack'):
            return False
        if rel.parent.name == 'gpu_dragon' and rel.name.endswith('_crc.bin'):
            return False
    return True

def inventory(root):
    root = Path(root)
    inputs = {}
    for folder in ('src', 'platform', 'resources', 'scripts', 'tests', 'newtests', 'docs', 'config', 'configs', 'gen', 'metadata', 'metadataconfig'):
        for p in sorted((root / folder).rglob('*')):
            rel = p.relative_to(root)
            if p.is_file() and included(rel):
                inputs[rel.as_posix()] = hashlib.sha256(p.read_bytes()).hexdigest()
    for rel in ('README.md', 'build.ps1', 'external_assets.manifest.json'):
        inputs[rel] = hashlib.sha256((root / rel).read_bytes()).hexdigest()
    # Root generator configurations are frozen and shipped as well.
    for p in sorted(root.iterdir()):
        if p.is_file() and p.suffix.lower() in ('.json', '.toml', '.yaml', '.yml', '.ini'):
            inputs[p.name] = hashlib.sha256(p.read_bytes()).hexdigest()
    return inputs

if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    current = inventory(root)
    if len(sys.argv) > 2 and sys.argv[2] == '--compare':
        before = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
        changed = sorted(k for k in set(before) | set(current) if before.get(k) != current.get(k))
        if changed:
            raise SystemExit('Inputs changed while building: ' + ', '.join(changed))
    else:
        Path(sys.argv[1]).write_text(json.dumps(current, indent=2), encoding='utf-8')
