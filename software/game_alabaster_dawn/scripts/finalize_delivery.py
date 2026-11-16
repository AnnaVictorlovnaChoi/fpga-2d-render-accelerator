"""Publish the independently proven fresh silent stage; never rewrite stage evidence."""
from pathlib import Path
import argparse,json,hashlib,shutil
from g225_contract import latest_stage,aggregate,MAP_VERSION
from pack_flash import full_image,validate,build,APP_LIMIT,FLASH_LIMIT
from external_manifest import create
from bin_to_hex import convert
sha=lambda b:hashlib.sha256(b).hexdigest()
def frozen(root,stage):
    from source_inputs import inventory
    inputs=json.loads((stage/'source_hashes.json').read_text(encoding='utf-8-sig'))
    if inventory(root)!=inputs: raise ValueError('Changed/added/removed since build; rebuild silent variant')
    return inputs

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--finalstop',required=True,choices=['source/resources/test'])
    for name in ('pack-manifest','pack','test-report'): p.add_argument('--'+name,required=True,type=Path)
    a=p.parse_args();r=Path(__file__).resolve().parents[1];stages={'silent':latest_stage(r,'silent')}
    inputs=frozen(r,stages['silent']);aggregate(r)
    assert a.test_report.resolve()==(r/'docs/test_g225_final.json').resolve(),'wrong aggregate'
    assert inputs['docs/test_g225_final.json']==sha(a.test_report.read_bytes()),'aggregate not frozen'
    manifest=json.loads(a.pack_manifest.read_text(encoding='utf-8-sig'));pack=a.pack.read_bytes()
    assert manifest==create(r) and pack==build(manifest,a.pack_manifest.resolve().parent),'pack not current source'
    entries=validate(pack,manifest.get('consumers',manifest['entries']))
    assert len(entries)==159 and sum(e['kind']==10 and e['id']==0 for e in entries)==1,'HUD10/159 required'
    for path in [a.pack_manifest]+[a.pack_manifest.parent/e['path'] for e in manifest['entries']]:
        assert path.resolve().relative_to(r).as_posix() in inputs,'unfrozen pack input'
    # Validate everything before publication.
    variants={}
    for variant,stage in stages.items():
        base=stage/r.name;bit=int(variant=='logged')
        evidence=json.loads((stage/'elf_verification.json').read_text());stack=json.loads((stage/'stack_report.json').read_text())
        assert evidence['status']=='PASS' and evidence['elf_sha256']==sha(Path(str(base)+'.elf').read_bytes()),'ELF evidence mismatch'
        assert stack['startup_4k_pass'] and stack['stack_bytes']==4096,'stack hard gate'
        assert evidence['runtime_log_enabled']==stack['runtime_log_enabled']==bit,'proof variant mismatch'
        assert evidence['project']==stack['project']==r.name and evidence['map_version']==stack['map_version']==MAP_VERSION,'old proof identity'
        proof=json.loads((stage/'build_manifest.json').read_text(encoding='utf-8-sig'))
        for ext in ('elf','bin','hex','map'):
            assert sha(Path(str(base)+'.'+ext).read_bytes()).upper()==proof['files'][ext].upper(),'artifact hash mismatch'
        assert frozen(r,stage)==inputs,'different source'
        variants[variant]=dict(stage=stage.name,runtime_log_enabled=bit,app_bytes=Path(str(base)+'.bin').stat().st_size)
    out=r/'deliverables';out.mkdir(exist_ok=True);names=[]
    (out/'commonassets.pack.bin').write_bytes(pack);names.append('commonassets.pack.bin')
    for variant,stage in stages.items():
        prefix=r.name+'.'+variant;base=stage/r.name;app=Path(str(base)+'.bin').read_bytes();full=full_image(app,pack)
        for suffix,data in [('app',app),('full.flash',full)]:
            target=out/(prefix+'.'+suffix+'.bin');target.write_bytes(data)
            convert(target,target.with_suffix('.hex'),FLASH_LIMIT);names.extend([target.name,target.with_suffix('.hex').name])
        for name in ('build_manifest.json','source_hashes.json','elf_verification.json','stack_report.json','compile_commands.json'):
            target=variant+'.'+name;shutil.copy2(stage/name,out/target);names.append(target)
        target=prefix+'.elf';shutil.copy2(Path(str(base)+'.elf'),out/target);names.append(target)
        variants[variant]['full_bytes']=len(full)
    for stage in stages.values(): assert frozen(r,stage)==inputs,'inputs changed during finalization'
    report=dict(project=r.name,status='PASS',variants=variants,shared_pack='commonassets.pack.bin',pack_bytes=len(pack),pack_flash_offset=APP_LIMIT,entries=entries,source_sha256=inputs,artifact_sha256={n:sha((out/n).read_bytes()) for n in names},test_report_sha256=sha(a.test_report.read_bytes()),map_version=MAP_VERSION,board_test='NOT_PERFORMED',board_cpu_budget='NOT_VERIFIED',functional_status='PASS')
    (r/'docs/DELIVERY_VERIFICATION.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('PASS: single silent stage finalized; board performance NOT_VERIFIED')
if __name__=='__main__':main()
