"""G225 identity, staging and functional release gate (not board timing proof)."""
from pathlib import Path
import json, os
PROJECT = 'G2.25_V10Display'
AGGREGATE = 'docs/test_g225_final.json'
MAP_VERSION = 'G225-ADFLASH3-159-ADWV1-HUD10'

def platform_contract(root):
    """Unique literal macro parsing; no inherited frequency/cache evidence."""
    import re
    text = (Path(root)/'platform/bsp/include/soc.h').read_text(encoding='utf-8-sig')
    def literal(name, expected):
        rows = re.findall(r'^\s*#define\s+'+name+r'\s+([^\r\n]+)', text, re.M)
        if len(rows)!=1 or not re.fullmatch(r'[0-9]+[uUlL]*', rows[0].strip()):
            raise ValueError('Missing/ambiguous/nonliteral soc.h macro: '+name)
        value=int(re.sub(r'[uUlL]+$', '', rows[0].strip()))
        if value != expected: raise ValueError('User-confirmed platform mismatch: '+name)
        return value
    hz=literal('SYSTEM_CLINT_HZ',300000000)
    return dict(clint_hz=hz,cpu_frequency_hz=hz,tick_frequency_hz=hz,icache_bytes=literal('SYSTEM_CORES_0_ICACHE_SIZE',8192),dcache_bytes=literal('SYSTEM_CORES_0_DCACHE_SIZE',16384),frequency_evidence='soc.h_unique_literal_assert_user_cpu300',cpu_config_profile='G221_CPU300_I8K_D16K')

def output_root(root):
    return Path(os.environ.get('G225_OUTPUT_ROOT') or (Path(root).parent / '临时开发1-性能增强' / 'g225' / 'build')).resolve()

def latest_stage(root, variant):
    bit = variant_bit(variant)
    stage = Path((output_root(root) / f'latest-success-log{bit}.txt').read_text(encoding='utf-8-sig').strip())
    stage = stage.resolve()
    if stage.parent != output_root(root) or not stage.name.startswith('run-') or not stage.name.endswith(f'-log{bit}'):
        raise ValueError('Formal stage must be a fresh run under G225_OUTPUT_ROOT')
    evidence = json.loads((stage / 'build_manifest.json').read_text(encoding='utf-8-sig'))
    if evidence.get('project') != PROJECT or evidence.get('map_version') != MAP_VERSION or evidence.get('finalstop') != 'source/resources/test':
        raise ValueError('Not a current G225 formal build')
    variant_contract(evidence, bit)
    source_flags_contract(root, stage, bit)
    performance_contract(evidence)
    if any(evidence.get(k)!=v for k,v in platform_contract(root).items()):
        raise ValueError('Fresh platform metadata differs from actual soc.h')
    from source_inputs import inventory
    if json.loads((stage/'source_hashes.json').read_text(encoding='utf-8-sig')) != inventory(root):
        raise ValueError('Latest stage does not match current frozen source')
    game_rules_contract(root)
    aggregate(root)
    return stage

def performance_contract(evidence):
    expected={'scheduler_wfi_enabled':0,'scheduler_wfi_supported':False,'cpu_deadline_polling':True,'scheduler_timer_irq_enabled':True,'deadline_clocksource':'CLINT','deadline_poll_source':'RAM_DUE_OR_VSYNC','sleep_feature_capability':'DISABLED_PENDING_USER_AUTHORIZATION','hardware_audit':'NEW_RTL_NOT_AUDITED','prior_hardware_audit':'RTL_NO_STALL_PATH','speed_scale':'3/5','wave_scale':'13/10','target_enemy':600,'target_enemy_unit':'count','cpu_budget_us':5000,'comparison':'strictly_less_than','cpu_budget_scope':'total_cpu_work','board_cpu_budget':'NOT_VERIFIED','wave_default_asset':0,'wave_default_profile':'standard'}
    expected.update(pipeline_model='one_pending_plus_render_ready',alpha_count_scope='exclude_persistent_private_cache_prep',opaque_diagnostic_glyphs=True)
    expected.update(alpha_display_scope='visible_game_objects_with_blended_support',sprite_display_policy='identity_to_400_then_double_excess',fps_display_policy='count_curve_58_59_60_below450_linear60to45_until600',display_policy_evidence='HUD_MAPPING_CONFIGURATION_NOT_MEASURED_FPS')
    expected.update(initial_hp=160,initial_coins=1000,max_waves=3,stamina=8,melee_action_scale='13/10',ranged_action_scale='3/2',action_scale_relative_to='G221',player_arrow_velocity_scale='unchanged')
    expected.update(clint_hz=300000000,cpu_frequency_hz=300000000,tick_frequency_hz=300000000,icache_bytes=8192,dcache_bytes=16384)
    if evidence.get('probe_enabled') != 0 or 'enemy_budget_us' in evidence or any(evidence.get(k)!=v for k,v in expected.items()):
        raise ValueError('Invalid target-count/CPU-units/default-wave metadata; old stage forbidden')

def required_file(root, relative):
    root = Path(root).resolve()
    path = (root / relative).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError('Required input outside project or missing: ' + str(relative))
    return path

def generator_inputs(root):
    # Reviewed dependency contract: route geometry is inline; LUT reads this header.
    # Output defaults are not required inputs and may point to external staging.
    import ast
    required_file(root, 'resources/player/fpga_player.h')
    required_file(root, 'resources/enemy/payload.bin')
    required_file(root, 'resources/command_templates/enemy_commands.inc')
    for relative in ('resources/player/manifest.json','resources/player/player.pack',
                     'resources/timing_source/g221_fpga_player.h','resources/timing_source/g221_arrow_data.c',
                     'resources/hud/source_images/stamina_base.png','resources/hud/source_images/params.json'):
        required_file(root, relative)
    for i in range(9):
        required_file(root, f'resources/hud/source_images/stamina_fill_{i}.png')
    import hashlib
    raster_names = [f'digit_{i}' for i in range(10)] + [f'wave_{i}' for i in range(4)] + [f'key_{i}_{j}' for i in range(3) for j in range(2)] + ['victory']
    hud_source=required_file(root,'scripts/generate_hud.py').read_text(encoding='utf-8-sig')
    # Reviewed actual locator contract, not merely a recursive existence scan.
    for locator in ("OUT=ROOT/'resources/hud'", "text=OUT/'source_images/raster_text'", "Image.open(text/(name+'.png'))", "raster(f'digit_{n}')", "raster(f'wave_{n}')", "raster(f'key_{k}_{on}')", "raster('victory')"):
        if locator not in hud_source:
            raise ValueError('HUD generator input locator changed; review required: '+locator)
    manifest=json.loads(required_file(root,'resources/hud/manifest.json').read_text(encoding='utf-8-sig'))
    source_pins={entry['path']:entry['sha256'] for entry in manifest['sources']}
    for name in raster_names:
        relative='source_images/raster_text/'+name+'.png'
        data=required_file(root,'resources/hud/'+relative).read_bytes()
        if not data.startswith(b'\x89PNG\r\n\x1a\n') or source_pins.get(relative)!=hashlib.sha256(data).hexdigest():
            raise ValueError('HUD raster input missing/changed/unpinned: '+relative)
    pins={'g221_fpga_player.h':'b25c68f7db885632f920546952aa54c9839e87b70d824841bf374b03a4ccda5c',
          'g221_arrow_data.c':'cc7d2e6cb57b82a435a9c4617e09538e0acb71910b38369fbf6f1e1c513d24ec'}
    for name,digest in pins.items():
        if hashlib.sha256(required_file(root,'resources/timing_source/'+name).read_bytes()).hexdigest()!=digest:
            raise ValueError('Immutable timing baseline changed: '+name)
    support=json.loads(required_file(root,'resources/alpha_support/manifest.json').read_text(encoding='utf-8-sig'))
    if hashlib.sha256(required_file(root,'resources/alpha_support/tables.h').read_bytes()).hexdigest()!=support['tables_sha256']:
        raise ValueError('Alpha support table hash mismatch')
    for name,entry in support['sources'].items():
        for relative,key in ((entry['packed_path'],'packed_sha256'),('resources/'+name+'/layout.h','layout_sha256'),('resources/'+name+'/external_streams.json','streams_sha256')):
            if hashlib.sha256(required_file(root,relative).read_bytes()).hexdigest()!=entry[key]:
                raise ValueError('Alpha support source changed: '+relative)
    for name in ('generate_enemy_routes.py', 'generate_render_tables.py', 'generate_wave.py', 'generate_command_templates.py', 'generate_action_timing.py', 'generate_hud.py', 'generate_alpha_support.py'):
        source = required_file(root, 'scripts/' + name).read_text(encoding='utf-8-sig')
        tree = ast.parse(source)
        for node in ast.walk(tree):
            if isinstance(node, (ast.Import, ast.ImportFrom)):
                modules = [a.name for a in node.names] if isinstance(node, ast.Import) else [node.module or '']
                if any(m.split('.')[0] in ('runpy', 'importlib') for m in modules):
                    raise ValueError('Generator must contain its implementation, not dynamic external loading: ' + name)
            if isinstance(node, ast.Call):
                func = node.func
                called = func.attr if isinstance(func, ast.Attribute) else func.id if isinstance(func, ast.Name) else ''
                if called in ('run_path', 'exec', 'eval', 'spec_from_file_location'):
                    raise ValueError('External implementation forwarding forbidden: ' + name)
        functions = {n.name for n in tree.body if isinstance(n, ast.FunctionDef)}
        expected = {'route', 'validate', 'main'} if name == 'generate_enemy_routes.py' else ({'generate'} if name != 'generate_wave.py' else {'main'})
        if not expected <= functions:
            raise ValueError('Missing bundled generator implementation: ' + name)

def self_contained(root):
    root = Path(root).resolve()
    generator_inputs(root)
    # Frozen HUD pixels may ship; proprietary installed fonts may not be copied.
    if list((root/'resources/hud').rglob('*.ttf')) or list((root/'resources/hud').rglob('*.otf')):
        raise ValueError('HUD font redistribution unresolved: remove private font copies before packaging')
    from external_manifest import create
    for entry in create(root)['entries']:
        required_file(root, entry['path'])

def aggregate(root):
    report = json.loads((Path(root) / AGGREGATE).read_text(encoding='utf-8-sig'))
    if report.get('status') != 'PASS' or report.get('project') != PROJECT:
        raise ValueError('G225 functional aggregate must PASS; board timing is separate')
    return report

if __name__ == '__main__':
    import sys
    aggregate(Path(sys.argv[1]))
    print('G225 functional aggregate PASS; CPU budget 5000us board NOT_VERIFIED')


def variant_bit(variant):
    if variant not in ('logged', 'silent', 0, 1):
        raise ValueError('Explicit logged/silent variant required')
    return {'logged':1, 'silent':0}.get(variant, variant)

def variant_contract(evidence, variant):
    bit = variant_bit(variant)
    if type(evidence.get('runtime_log_enabled')) is not int or evidence['runtime_log_enabled'] != bit:
        raise ValueError('Wrong runtime_log_enabled variant')
    flags=evidence.get('compile_flags', [])
    for name,value in [('AD_RUNTIME_LOG',bit),('AD_PERF_VERBOSE',0),('AD_CPU_PROBE',0)]:
        if [f for f in flags if f.startswith('-D'+name+'=')] != [f'-D{name}={value}']:
            raise ValueError('Missing/ambiguous/wrong global source flags: '+name)

def source_flags_contract(root, stage, bit):
    import hashlib
    root,stage=Path(root),Path(stage)
    rows=json.loads((stage/'compile_commands.json').read_text(encoding='utf-8-sig'))
    expected={p.relative_to(root).as_posix() for p in (root/'src').rglob('*') if p.is_file() and p.suffix in ('.c','.S') and p.name not in ('runtime_blob.S','arrow_blob.S','scene_blob.S','dragon_blob.S','assets_blob.S')}
    expected.update(('platform/gpu/2D_DMA.c','platform/common/start.S','platform/common/trap.S','platform/common/syscalls.c'))
    if len(rows)!=len(expected) or {row['source'] for row in rows}!=expected:
        raise ValueError('Actual compiled TU inventory differs from complete source glob')
    config=json.loads((stage/'build_config.json').read_text(encoding='utf-8-sig'))
    variant_contract(config, bit)
    for row in rows:
        variant_contract(dict(runtime_log_enabled=bit,compile_flags=row['flags']),bit)
        if [f for f in row['flags'] if not f.startswith('-I')]!=config['compile_flags']:
            raise ValueError('Per-TU flags differ from global source flags')
        obj=stage/(row['source']+'.o')
        if hashlib.sha256(obj.read_bytes()).hexdigest().upper()!=row['object_sha256'].upper():
            raise ValueError('Per-TU object hash mismatch')

def game_rules_contract(root):
    import re
    text=(Path(root)/'src/game_rules.h').read_text(encoding='utf-8-sig')
    values=re.findall(r'^\s*#define\s+GAME_RULES_MAX_HP\s+(\d+)u?\s*$',text,re.M)
    if values != ['160']:
        raise ValueError('GAME_RULES_MAX_HP must uniquely equal initial_hp160')

def stage_pair(root):
    """Compatibility API: release contains only the current silent stage."""
    return {'silent': latest_stage(root, 'silent')}
