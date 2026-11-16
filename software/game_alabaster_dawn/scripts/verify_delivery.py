"""Verify the silent variant, streaming HEX roundtrips, and exact single-project ZIP."""
from pathlib import Path
import hashlib,json,zipfile
from g225_contract import latest_stage,aggregate
from pack_flash import validate,APP_LIMIT,build as build_pack
from external_manifest import create
from finalize_delivery import frozen
sha=lambda b:hashlib.sha256(b).hexdigest()
def checkhex(path,data):
    count=0
    with path.open('rb') as f:
        for line in f:
            value=line.strip()
            assert len(value)==2 and all(c in b'0123456789abcdefABCDEF' for c in value),'not byte-per-line HEX'
            assert count<len(data) and int(value,16)==data[count],'HEX/BIN mismatch'
            count+=1
    assert count==len(data),'HEX length mismatch'
def verify(root,check_zip=True):
    r=Path(root);out=r/'deliverables';stages={'silent':latest_stage(r,'silent')};inputs=frozen(r,stages['silent'])
    report=json.loads((r/'docs/DELIVERY_VERIFICATION.json').read_text())
    assert report['project']==r.name and report['status']=='PASS' and report['source_sha256']==inputs
    assert set(report['variants'])=={'silent'}
    assert sha((r/'docs/test_g225_final.json').read_bytes())==report['test_report_sha256'];aggregate(r)
    pack=(out/'commonassets.pack.bin').read_bytes();validate(pack)
    assert pack==build_pack(create(r),r),'pack differs from current streams'
    for variant,stage in stages.items():
        assert report['variants'][variant]['stage']==stage.name,'swapped stage'
        assert report['variants'][variant]['runtime_log_enabled']==int(variant=='logged'),'wrong variant'
        prefix=r.name+'.'+variant;base=stage/r.name
        app=(out/(prefix+'.app.bin')).read_bytes();full=(out/(prefix+'.full.flash.bin')).read_bytes()
        assert app==Path(str(base)+'.bin').read_bytes(),'stage APP mismatch'
        evidence=json.loads((stage/'build_manifest.json').read_text(encoding='utf-8-sig'))
        for ext,digest in evidence['files'].items(): assert sha(Path(str(base)+'.'+ext).read_bytes()).upper()==digest.upper()
        assert 0<len(app)<=APP_LIMIT and full==app+b'\xff'*(APP_LIMIT-len(app))+pack,'APP/gap/pack mismatch'
        checkhex(out/(prefix+'.app.hex'),app);checkhex(out/(prefix+'.full.flash.hex'),full)
        for name in ('build_manifest.json','source_hashes.json','elf_verification.json','stack_report.json','compile_commands.json'):
            assert (out/(variant+'.'+name)).read_bytes()==(stage/name).read_bytes(),'variant proof mismatch'
        assert (out/(prefix+'.elf')).read_bytes()==Path(str(base)+'.elf').read_bytes()
    for name,digest in report['artifact_sha256'].items(): assert sha((out/name).read_bytes())==digest,name
    if check_zip:
        archive=out/(r.name+'_project.zip')
        with zipfile.ZipFile(archive) as z:
            prefix=r.name+'/';members=[n for n in z.namelist() if not n.endswith('/')]
            expected={prefix+rel for rel in inputs}|{prefix+'docs/DELIVERY_VERIFICATION.json'}|{prefix+'deliverables/'+n for n in report['artifact_sha256']}
            assert len(members)==len(set(members)) and set(members)==expected,'ZIP missing/extra/duplicate members'
            assert z.read(prefix+'docs/DELIVERY_VERIFICATION.json')==(r/'docs/DELIVERY_VERIFICATION.json').read_bytes()
            for rel,digest in inputs.items(): assert sha(z.read(prefix+rel))==digest,rel
            for name,digest in report['artifact_sha256'].items(): assert sha(z.read(prefix+'deliverables/'+name))==digest,name
    for stage in stages.values(): assert frozen(r,stage)==inputs
    return dict(status='PASS',variants=['silent'],source_files=len(inputs),board_test='NOT_PERFORMED',board_cpu_budget='NOT_VERIFIED')
if __name__=='__main__':
    r=Path(__file__).resolve().parents[1];result=verify(r)
    (r/'docs/ARCHIVE_VERIFICATION.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
