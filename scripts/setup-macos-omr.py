#!/usr/bin/env python3
"""Install the pinned Apple Silicon native OMR engine and Transcoda model."""
from pathlib import Path
import hashlib
import json
import shutil
import tarfile
import urllib.request
root = Path(__file__).resolve().parents[1] / 'build' / '_deps' / 'CrispEmbed'
runtime = root / 'runtime'
(runtime / 'models').mkdir(parents=True, exist_ok=True)
(runtime / 'licenses').mkdir(exist_ok=True)
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''): h.update(block)
    return h.hexdigest()
def fetch(url,path,expected):
    if path.is_file() and digest(path)==expected: return
    print('Downloading',path.name,flush=True)
    partial=path.with_suffix(path.suffix+'.part')
    with urllib.request.urlopen(url,timeout=120) as source,partial.open('wb') as out: shutil.copyfileobj(source,out)
    if digest(partial)!=expected: raise RuntimeError('Hash mismatch: '+path.name)
    partial.replace(path)
version='v0.17.12'
archive=root/'macos-arm64.tar.gz'
url='https://github.com/CrispStrobe/CrispEmbed/releases/download/'+version+'/crispembed-macos-arm64.tar.gz'
archiveHash='6bdbf65587eac063ec6658d9e03345a63614cee680ea887ef51eaabcc72f7a56'
fetch(url,archive,archiveHash)
with tarfile.open(archive) as tar:
    for member in tar.getmembers():
        name=Path(member.name).name
        if member.name not in ('./'+name,name) or not (name=='crispembed' or name.endswith('.dylib') or name=='LICENSE'): continue
        target=runtime/name if name!='LICENSE' else runtime/'licenses'/'CRISPEMBED-LICENSE.txt'
        if member.issym():
            if Path(member.linkname).name!=member.linkname: raise RuntimeError('Unsafe archive link')
            if target.is_symlink(): target.unlink()
            target.symlink_to(member.linkname)
        elif member.isfile():
            if target.is_symlink():target.unlink()
            with tar.extractfile(member) as source,target.open('wb') as out:shutil.copyfileobj(source,out)
            if name=='crispembed':target.chmod(0o755)
revision='c4ed06a104f30d19503aa2ef8f79a23152167faa'
modelHash='a6a977676339a7add577aa95089c2d30fb912b185ef117b068fe826e91a8a283'
modelURL='https://huggingface.co/cstr/transcoda-omr-GGUF/resolve/'+revision+'/transcoda-q8_0.gguf'
fetch(modelURL,runtime/'models'/'staff.gguf',modelHash)
for name,source,sha in [
 ('GGML-LICENSE.txt','https://raw.githubusercontent.com/CrispStrobe/ggml/890278a8342c620197c90e702e1188bcab94f510/LICENSE','94f29bbed6a22c35b992c5c6ebf0e7c92f13b836b90f36f461c9cf2f0f1d010d'),
 ('TRANSCODA-MODEL-CARD.md','https://huggingface.co/cstr/transcoda-omr-GGUF/raw/'+revision+'/README.md','8c0fb26a862c6087e512bab263828bfdb9c7c08dff8c01427218eb561cfa0940')]:fetch(source,runtime/'licenses'/name,sha)
(runtime/'licenses'/'CC-BY-4.0-LINK.txt').write_text('Creative Commons Attribution 4.0 International\nhttps://creativecommons.org/licenses/by/4.0/\nFull legal terms: https://creativecommons.org/licenses/by/4.0/legalcode\n')
(runtime/'licenses'/'SOURCE.txt').write_text('CrispEmbed '+version+' macOS arm64 and ggml: MIT.\nTranscoda-59M-Q8: CC-BY-4.0, original by btrkeks; GGUF conversion by CrispStrobe/cstr.\nEngine source commit: 78493a31fc3043f3af4ab6fd2f197a32da6f07f0\n'+url+'\n'+modelURL+'\n')
(runtime/'runtime-manifest.json').write_text(json.dumps({'schemaVersion':1,'engineVersion':version,'engineCommit':'78493a31fc3043f3af4ab6fd2f197a32da6f07f0','backend':'metal','modelName':'Transcoda-59M-Q8','modelRevision':revision,'modelSha256':modelHash,'modelLicense':'CC-BY-4.0','modelPath':'models/staff.gguf','archiveSha256':archiveHash},indent=2))
print('Verified Apple Silicon native OMR',flush=True)
