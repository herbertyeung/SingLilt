#!/usr/bin/env python3
"""Fetch the same verified sound banks as Windows, without Windows runtimes."""
import hashlib
from pathlib import Path
import shutil
import tarfile
import urllib.request
root = Path(__file__).resolve().parents[1] / 'build' / '_deps' / 'SoundFonts'
root.mkdir(parents=True, exist_ok=True)
def digest(path):
    sha = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            sha.update(block)
    return sha.hexdigest()
def fetch(url, path, expected):
    if path.exists() and digest(path) == expected:
        return
    temporary = path.with_suffix(path.suffix + '.part')
    print('Downloading', path.name, flush=True)
    with urllib.request.urlopen(url, timeout=120) as response, temporary.open('wb') as output:
        shutil.copyfileobj(response, output)
    if digest(temporary) != expected:
        raise RuntimeError('SHA256 mismatch: ' + str(temporary))
    temporary.replace(path)
fetch('https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/684543d5e5efaef08d02be50dcda8d552478fa60/GeneralUser-GS.sf2', root / 'GeneralUser-GS.sf2', '9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe')
piano = root / 'Salamander.sf2'
expected = '712d0e681efbe5203a8014e9b3e84168f1908c82f2f6fb13bd2c77d6d72c70b7'
if not piano.exists() or digest(piano) != expected:
    archive = root / 'Salamander.tar.xz'
    fetch('https://freepats.zenvoid.org/Piano/SalamanderGrandPiano/SalamanderGrandPiano-SF2-V3%2B20200602.tar.xz', archive, '15edb061d7ba60d58332f72dba8f8ce40988048cc703f935e6320f37d650e213')
    with tarfile.open(archive, 'r:xz') as tar:
        # Copy only the expected regular file; never extract archive paths.
        member = tar.getmember('SalamanderGrandPiano-SF2-V3+20200602/SalamanderGrandPiano-V3+20200602.sf2')
        if not member.isfile() or member.size > 2 * 1024**3:
            raise RuntimeError('Invalid piano archive member')
        with tar.extractfile(member) as source, piano.with_suffix('.part').open('wb') as output:
            shutil.copyfileobj(source, output)
    if digest(piano.with_suffix('.part')) != expected:
        raise RuntimeError('Piano SHA256 mismatch')
    piano.with_suffix('.part').replace(piano)
print('Verified Salamander and GeneralUser GS', flush=True)
