[CmdletBinding()]
param([string]$OutputDirectory, [string]$OutputRoot = $env:G225_OUTPUT_ROOT)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path $PSScriptRoot -Parent
if (-not $OutputRoot) { $OutputRoot=Join-Path (Split-Path $root -Parent) '临时开发1-性能增强/g225/build' }
$env:G225_OUTPUT_ROOT=$OutputRoot
$env:PYTHONPYCACHEPREFIX=Join-Path $OutputRoot 'pycache'
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $root 'deliverables' }
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$env:G225_ARCHIVE_DIRECTORY=$OutputDirectory
Push-Location $root
try {
    # Stream one project directly into ZIP: no multi-hundred-MB staging copy.
    $code=@'
from pathlib import Path
import os,json,zipfile,hashlib,sys
sys.path.insert(0,'scripts')
from g225_contract import self_contained
from verify_delivery import verify
r=Path.cwd();self_contained(r);verify(r,check_zip=False)
d=json.loads((r/'docs/DELIVERY_VERIFICATION.json').read_text())
archive=Path(os.environ['G225_ARCHIVE_DIRECTORY'])/(r.name+'_project.zip')
files={rel:digest for rel,digest in d['source_sha256'].items()}
files['docs/DELIVERY_VERIFICATION.json']=hashlib.sha256((r/'docs/DELIVERY_VERIFICATION.json').read_bytes()).hexdigest()
files.update({'deliverables/'+n:h for n,h in d['artifact_sha256'].items()})
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for rel,digest in files.items():
        data=(r/rel).read_bytes()
        if hashlib.sha256(data).hexdigest()!=digest: raise ValueError('Changed during packaging: '+rel)
        z.writestr(r.name+'/'+rel,data)
print(archive)
'@
    & python -c $code
    if ($LASTEXITCODE -ne 0) { throw 'Archive/current source validation failed' }
} finally { Pop-Location }
