# Pinned FluidSynth runtime and sampled-piano setup.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

# Pinned, project-local audio dependencies. Requires Python 3.12+ (stdlib only).
# Archives and extraction stay in build/; this creates no CMake projects.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$python = (Get-Command python -ErrorAction Stop).Source
$installer = @'
import hashlib
import json
import pathlib
import shutil
import stat
import sys
import tarfile
import urllib.request
import zipfile

if sys.version_info < (3, 12):
    raise SystemExit('Python 3.12 or newer is required for safe tar extraction.')
root = pathlib.Path(sys.argv[1]).resolve()
fluid_root = root / 'build' / '_deps' / 'FluidSynth'
sound_root = root / 'build' / '_deps' / 'SoundFonts'
fluid_name = 'fluidsynth-v2.6.1-win10-x64-cpp11'
sound_name = 'SalamanderGrandPiano-SF2-V3+20200602'
sound_file = 'SalamanderGrandPiano-V3+20200602.sf2'
fluid_url = 'https://github.com/FluidSynth/fluidsynth/releases/download/v2.6.1/' + fluid_name + '.zip'
sound_url = 'https://freepats.zenvoid.org/Piano/SalamanderGrandPiano/SalamanderGrandPiano-SF2-V3%2B20200602.tar.xz'
fluid_hash = 'fab7a2e4b85675b66970f97a39bbc239729c5e0f237198b5922a6a73cbc8677c'
sound_hash = '15edb061d7ba60d58332f72dba8f8ce40988048cc703f935e6320f37d650e213'
sf2_hash = '712d0e681efbe5203a8014e9b3e84168f1908c82f2f6fb13bd2c77d6d72c70b7'
readme_hash = 'ed3a0ccd16573a8e72966bdfb14bf4b3e18fab94bb9e36da1aa6409522d359f8'

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def fetch(url, destination, expected):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file():
        if digest(destination) != expected:
            raise RuntimeError('SHA256 mismatch in cached file: ' + str(destination))
        print('Verified cache:', destination.name, flush=True)
        return
    partial = destination.with_name(destination.name + '.part')
    print('Downloading:', url, flush=True)
    request = urllib.request.Request(url, headers={'User-Agent': 'SingLilt-dependency-setup'})
    with urllib.request.urlopen(request, timeout=120) as response, partial.open('wb') as output:
        shutil.copyfileobj(response, output, 1024 * 1024)
    if digest(partial) != expected:
        raise RuntimeError('Downloaded SHA256 mismatch; retained partial file: ' + str(partial))
    partial.replace(destination)
    print('Verified download:', destination.name, flush=True)

def checked_target(parent, name):
    path = pathlib.PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
        raise RuntimeError('Rejected archive path: ' + name)
    target = (parent / name).resolve()
    if not target.is_relative_to(parent.resolve()):
        raise RuntimeError('Archive path leaves dependency directory: ' + name)
    return target

fluid_archive = fluid_root / 'cache' / (fluid_name + '.zip')
sound_archive = sound_root / 'cache' / (sound_name + '.tar.xz')
fetch(fluid_url, fluid_archive, fluid_hash)
fetch(sound_url, sound_archive, sound_hash)

# Inspect every entry before extraction; reject links and special files.
with zipfile.ZipFile(fluid_archive) as archive:
    entries = archive.infolist()
    if sum(entry.file_size for entry in entries) > 256 * 1024 * 1024:
        raise RuntimeError('Unexpected FluidSynth archive expansion size.')
    for entry in entries:
        checked_target(fluid_root, entry.filename)
        if stat.S_ISLNK(entry.external_attr >> 16):
            raise RuntimeError('Zip links are not accepted: ' + entry.filename)
    installed = True
    for entry in entries:
        if entry.is_dir():
            continue
        destination = fluid_root / entry.filename
        expected = hashlib.sha256(archive.read(entry)).hexdigest()
        if not destination.is_file() or digest(destination) != expected:
            installed = False
            break
    if not installed:
        print('Extracting verified FluidSynth archive...', flush=True)
        archive.extractall(fluid_root)
    for entry in entries:
        if not entry.is_dir() and digest(fluid_root / entry.filename) != hashlib.sha256(archive.read(entry)).hexdigest():
            raise RuntimeError('FluidSynth extraction verification failed: ' + entry.filename)

sf2 = sound_root / sound_name / sound_file
readme = sound_root / sound_name / 'readme.txt'
if not (sf2.is_file() and digest(sf2) == sf2_hash and readme.is_file() and digest(readme) == readme_hash):
    print('Inspecting Salamander archive (1.18 GiB uncompressed)...', flush=True)
    with tarfile.open(sound_archive, 'r:xz') as archive:
        members = archive.getmembers()
        if sum(member.size for member in members) > 2 * 1024 * 1024 * 1024:
            raise RuntimeError('Unexpected Salamander archive expansion size.')
        for member in members:
            checked_target(sound_root, member.name)
            if not (member.isfile() or member.isdir()):
                raise RuntimeError('Tar links/special files are not accepted: ' + member.name)
        print('Extracting verified Salamander archive...', flush=True)
        archive.extractall(sound_root, members=members, filter='data')
if digest(sf2) != sf2_hash or digest(readme) != readme_hash:
    raise RuntimeError('Salamander extraction verification failed.')

# Verbatim licenses are tracked in the repository. Fetch pinned originals only
# when missing. The Windows system GM soundbank is never copied or packaged here.
licenses = [
    ('LICENSE.txt', 'https://raw.githubusercontent.com/FluidSynth/fluidsynth/v2.6.1/LICENSE',
     '20e50fe7aae3e56378ebf0417d9de904f55a0e61e4df315333e632a4d3555d95'),
    ('AUTHORS.txt', 'https://raw.githubusercontent.com/FluidSynth/fluidsynth/v2.6.1/AUTHORS',
     '87fb43b0172262d21a4d39eb1598f8337491d2559ab7039b7aa7c1311e8ddb4e'),
    ('SDL3-LICENSE.txt', 'https://raw.githubusercontent.com/libsdl-org/SDL/release-3.2.10/LICENSE.txt',
     '97f35b302b361680ec1e891e95d2d52097bb95abff361434916d99dc1305f127'),
    ('libsndfile-LICENSE.txt', 'https://raw.githubusercontent.com/libsndfile/libsndfile/1.2.2/COPYING',
     'ad01ea5cd2755f6048383c8d54c88459cd6fcb17757c5c8892f8c5ea060f6140'),
]
for name, url, sha in licenses:
    fetch(url, root / 'licenses' / 'fluidsynth' / name, sha)
(root / 'licenses' / 'salamander').mkdir(parents=True, exist_ok=True)
shutil.copyfile(readme, root / 'licenses' / 'salamander' / 'UPSTREAM-README.txt')

# These facts were parsed from the pinned SF2 phdr/igen/shdr records, not inferred
# from the original SFZ description. Hash verification binds them to this bank.
velocity = [[1,26],[27,34],[35,36],[37,43],[44,46],[47,50],[51,56],[57,64],
            [65,72],[73,80],[81,88],[89,96],[97,104],[105,112],[113,120],[121,127]]
metadata = {
    'source_page': 'https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html',
    'archive_url': sound_url, 'archive_sha256': sound_hash, 'archive_bytes': sound_archive.stat().st_size,
    'file': str(sf2), 'sha256': sf2_hash, 'bytes': sf2.stat().st_size,
    'presets': [{'name': 'Instrument', 'program': 0, 'bank': 0}],
    'velocity_ranges': velocity, 'velocity_layer_count': 16,
    'sample_count': 960, 'sample_rates': [48000], 'sample_types': [2, 4],
    'license': 'CC-BY-3.0', 'author': 'Alexander Holm', 'sf2_adaptation': 'Roberto, FreePats project',
}
(sound_root / 'metadata.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
manifest = {
    'version': '2.6.1', 'archive_url': fluid_url, 'archive_sha256': fluid_hash,
    'archive_bytes': fluid_archive.stat().st_size, 'root': str(fluid_root / fluid_name),
    'include': str(fluid_root / fluid_name / 'include'),
    'import_library': str(fluid_root / fluid_name / 'lib' / 'libfluidsynth-3.lib'),
    'runtime_dlls': ['libfluidsynth-3.dll', 'SDL3.dll', 'sndfile.dll'],
}
(fluid_root / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('READY FluidSynth:', manifest['root'])
print('READY Salamander:', sf2)
print('SoundFont: bank 0 / program 0; 16 velocity layers; bytes=' + str(sf2.stat().st_size))
'@
$installer | & $python - $root
if ($LASTEXITCODE -ne 0) { throw "Audio dependency setup failed with exit code $LASTEXITCODE" }
