# Pinned Whisper runtime and lyric-model setup.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$installer = @'
import pathlib,hashlib,urllib.request,shutil,zipfile,sys
root=pathlib.Path(sys.argv[1]).resolve()
directory=root/'build/_deps/Whisper'
directory.mkdir(parents=True,exist_ok=True)
def fetch(url,destination,expected):
    destination.parent.mkdir(parents=True,exist_ok=True)
    if not destination.exists():
        partial=destination.with_suffix(destination.suffix+'.part')
        with urllib.request.urlopen(url,timeout=180) as response,partial.open('wb') as stream:
            shutil.copyfileobj(response,stream,1024*1024)
        with partial.open('rb') as stream:
            if hashlib.file_digest(stream,'sha256').hexdigest()!=expected:
                raise RuntimeError('Downloaded SHA256 mismatch: '+destination.name)
        partial.replace(destination)
    with destination.open('rb') as stream:
        if hashlib.file_digest(stream,'sha256').hexdigest()!=expected:
            raise RuntimeError('Cached SHA256 mismatch: '+destination.name)
    print('VERIFIED AUDIO:',destination.name)
archive=directory/'whisper-bin-x64-v1.8.3.zip'
fetch('https://github.com/ggml-org/whisper.cpp/releases/download/v1.8.3/whisper-bin-x64.zip',archive,
      'd824b1e37599f882b396e73f1ee0bfd5d0529f700314c48311dcbd00b803321d')
runtime=directory/'runtime'
with zipfile.ZipFile(archive) as package:
    for entry in package.infolist():
        path=pathlib.PurePosixPath(entry.filename)
        if path.is_absolute() or '..' in path.parts or '\\' in entry.filename or ':' in entry.filename:
            raise RuntimeError('Unexpected archive path')
        if not (runtime/entry.filename).resolve().is_relative_to(runtime.resolve()):
            raise RuntimeError('Archive path leaves runtime')
    package.extractall(runtime)
fetch('https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin',directory/'models/ggml-base.bin',
      '60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe')
fetch('https://raw.githubusercontent.com/ggml-org/whisper.cpp/v1.8.3/LICENSE',root/'licenses/whisper/LICENSE.txt',
      'e562a2ddfaf8280537795ac5ecd34e3012b6582a147ef69ba6a6a5c08c84757d')
fetch('https://raw.githubusercontent.com/openai/whisper/main/LICENSE',root/'licenses/whisper/MODEL-LICENSE.txt',
      'b5d65a59060e68c4ff940e1eddfa6f94b2d68fdf58ed7f4dd57721c997e35e9d')
'@
$installer | python - $root
if ($LASTEXITCODE -ne 0) { throw 'Local audio tool setup failed' }
