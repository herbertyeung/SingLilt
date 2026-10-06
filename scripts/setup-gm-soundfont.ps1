# Pinned GeneralUser GM sound-bank setup.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$installer = @'
import hashlib
import pathlib
import urllib.request
import sys

root = pathlib.Path(sys.argv[1]).resolve()
commit = '684543d5e5efaef08d02be50dcda8d552478fa60'
base = 'https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/' + commit + '/'
directory = root / 'build' / '_deps' / 'SoundFonts' / 'GeneralUser-GS'
directory.mkdir(parents=True, exist_ok=True)
files = [
    ('GeneralUser-GS.sf2', '9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe'),
    ('documentation/LICENSE.txt', '7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be'),
    ('README.md', 'f1a5d1ef99591763617689d064e57113b1db900a920e145233aa2789331e085a'),
]
for relative, expected in files:
    destination = directory / pathlib.PurePosixPath(relative).name
    if not destination.exists():
        partial = destination.with_suffix(destination.suffix + '.part')
        with urllib.request.urlopen(base + relative, timeout=120) as response, partial.open('wb') as stream:
            import shutil
            shutil.copyfileobj(response, stream)
        if hashlib.file_digest(partial.open('rb'), 'sha256').hexdigest() != expected:
            raise RuntimeError('SHA256 mismatch: ' + relative)
        partial.replace(destination)
    with destination.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != expected:
            raise RuntimeError('Cached SHA256 mismatch: ' + relative)
    print('VERIFIED GeneralUser GS:', destination.name)
'@
$installer | python - $root
if ($LASTEXITCODE -ne 0) { throw 'GeneralUser GS setup failed' }
