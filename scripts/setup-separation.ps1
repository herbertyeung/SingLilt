# Local Python runtime and vocal-separation model setup.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build'),
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetFullPath($BuildDirectory)) '_deps\Separation'
$downloads = Join-Path $root 'downloads'
$runtime = Join-Path $root 'runtime'
$pythonDirectory = Join-Path $runtime 'python'
$packages = Join-Path $pythonDirectory 'Lib\site-packages'
$models = Join-Path $runtime 'models'
$licenses = Join-Path $PSScriptRoot '..\licenses\separation'
New-Item -ItemType Directory -Force $downloads,$pythonDirectory,$packages,$models,$licenses | Out-Null

function Run-Python([string[]]$Arguments) {
    & $Python @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Python command failed with exit $LASTEXITCODE" }
}
function Download-Verified([string]$Url, [string]$Path, [string]$HashPrefix) {
    if (-not (Test-Path -LiteralPath $Path)) {
        $temporary = "$Path.downloading"
        Invoke-WebRequest -Uri $Url -OutFile $temporary
        Move-Item -LiteralPath $temporary -Destination $Path -Force
    }
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($HashPrefix -and -not $hash.StartsWith($HashPrefix)) { throw "Checksum mismatch for $Path" }
    return $hash
}

$env:PYTHONUTF8 = '1'
$pythonVersion = & $Python -c 'import sys; print(".".join(map(str,sys.version_info[:3])))'
if ($LASTEXITCODE -ne 0 -or $pythonVersion.Trim() -ne '3.12.10') {
    throw 'Bootstrap requires Python 3.12.10 x64; it installs packages only under the build directory.'
}
$pythonUrl = 'https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip'
$pythonZip = Join-Path $downloads 'python-3.12.10-embed-amd64.zip'
$pythonHash = Download-Verified $pythonUrl $pythonZip '4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3'
if ((Get-FileHash -LiteralPath $pythonZip -Algorithm MD5).Hash.ToLowerInvariant() -ne 'fe8ef205f2e9c3ba44d0cf9954e1abd3') {
    throw 'The Python archive does not match the release-page checksum.'
}
if (-not (Test-Path -LiteralPath (Join-Path $pythonDirectory 'python.exe'))) {
    Expand-Archive -LiteralPath $pythonZip -DestinationPath $pythonDirectory -Force
}
@('python312.zip','.','Lib/site-packages') |
    Set-Content -LiteralPath (Join-Path $pythonDirectory 'python312._pth') -Encoding ascii

$requirements = @('demucs-infer==4.2.2','numpy==1.26.4','soundfile==0.13.1','einops==0.8.1',
    'PyYAML==6.0.2','tqdm==4.66.5','cffi==1.17.1','pycparser==2.22','filelock==3.16.1',
    'typing_extensions==4.12.2','sympy==1.13.1','mpmath==1.3.0','networkx==3.3',
    'Jinja2==3.1.4','MarkupSafe==2.1.5','fsspec==2024.9.0','setuptools==70.3.0','colorama==0.4.6')
$allRequirements = @('torch==2.4.1+cpu','torchaudio==2.4.1+cpu','julius==0.2.7') + $requirements
$packagesReady = $false
if (Test-Path -LiteralPath (Join-Path $packages 'torch\__init__.py')) {
    & (Join-Path $pythonDirectory 'python.exe') -c 'import importlib.metadata as m,sys;assert all(m.version(p.split("==")[0])==p.split("==")[1] for p in sys.argv[1:])' @allRequirements
    $packagesReady = $LASTEXITCODE -eq 0
}
if (-not $packagesReady) {
    Run-Python -Arguments @('-m','pip','download','--disable-pip-version-check','--no-deps','--dest',$downloads,
        '--index-url','https://download.pytorch.org/whl/cpu','torch==2.4.1+cpu','torchaudio==2.4.1+cpu')
    Run-Python -Arguments (@('-m','pip','download','--disable-pip-version-check','--no-deps','--only-binary=:all:',
        '--dest',$downloads) + $requirements)
    Run-Python -Arguments @('-m','pip','wheel','--disable-pip-version-check','--no-deps','--wheel-dir',$downloads,'julius==0.2.7')
    Run-Python -Arguments (@('-m','pip','install','--disable-pip-version-check','--no-index','--find-links',$downloads,
        '--target',$packages,'--no-compile','--upgrade','--no-deps') + $allRequirements)
}

$modelFile = '955717e8-8726e21a.th'
$modelUrl = "https://dl.fbaipublicfiles.com/demucs/hybrid_transformer/$modelFile"
$modelHash = Download-Verified $modelUrl (Join-Path $models $modelFile) '8726e21a993978c7ba086d3872e7608d7d5bfca646ca4aca459ffda844faa8b4'
[ordered]@{schema=1;name='htdemucs';signature='955717e8';file=$modelFile;sha256=$modelHash;
    version='demucs-infer-4.2.2';url=$modelUrl;license='MIT'} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $models 'model.json') -Encoding utf8
"models: ['955717e8']" | Set-Content -LiteralPath (Join-Path $models 'htdemucs.yaml') -Encoding ascii
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'separate_vocals.py') -Destination $runtime -Force
Copy-Item -LiteralPath (Join-Path $pythonDirectory 'LICENSE.txt') -Destination (Join-Path $licenses 'PYTHON-LICENSE.txt') -Force

$records = @([ordered]@{name='python-3.12.10-embed-amd64.zip';url=$pythonUrl;sha256=$pythonHash;license='PSF-2.0'},
    [ordered]@{name=$modelFile;url=$modelUrl;sha256=$modelHash;license='MIT'})
Get-ChildItem -LiteralPath $downloads -Filter '*.whl' | ForEach-Object {
    $records += [ordered]@{name=$_.Name;source='PyPI or download.pytorch.org (pinned requirements)';
        sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $licenses 'SOURCES.json') -Encoding utf8
$noticeScript = @'
from pathlib import Path
import importlib.metadata as metadata,json,shutil
root=Path(__import__('sys').argv[1]);dest=Path(__import__('sys').argv[2]);records=[]
for dist in metadata.distributions(path=[str(root)]):
    name=dist.metadata['Name'];version=dist.version
    records.append({'name':name,'version':version,'license':dist.metadata.get('License',''),
        'homepage':dist.metadata.get('Home-page',''),'projectURLs':dist.metadata.get_all('Project-URL',[])})
    for entry in dist.files or []:
        if 'license' in entry.name.lower() or 'copying' in entry.name.lower() or 'notice' in entry.name.lower():
            source=dist.locate_file(entry)
            if source.is_file():
                output=dest/name/str(entry);output.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,output)
(dest/'DEPENDENCIES.json').write_text(json.dumps(records,indent=2,ensure_ascii=False),encoding='utf-8')
'@
Run-Python -Arguments @('-c',$noticeScript,$packages,$licenses)
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/facebookresearch/demucs/main/LICENSE' -OutFile (Join-Path $licenses 'DEMUCS-MIT.txt')
& (Join-Path $pythonDirectory 'python.exe') -c 'import sys,torch,torchaudio,numpy,soundfile,demucs_infer;print(sys.version);print(torch.__version__,torchaudio.__version__,numpy.__version__,soundfile.__version__);assert torch.__version__=="2.4.1+cpu";assert "MP3" in soundfile.available_formats();assert torch.from_numpy(numpy.ones(3,dtype="float32")).numpy().sum()==3;assert all(str(__import__("pathlib").Path(sys.executable).parent) in path for path in sys.path)'
if ($LASTEXITCODE -ne 0) { throw 'Portable separation runtime verification failed.' }
New-Item -ItemType Directory -Force (Join-Path $runtime 'licenses') | Out-Null
Get-ChildItem -LiteralPath $licenses | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $runtime 'licenses') -Recurse -Force
}
Write-Host "Separation runtime: $runtime"
Write-Host "Checkpoint SHA256: $modelHash"
