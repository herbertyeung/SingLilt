#!/usr/bin/env python3
"""Verify a relocated macOS bundle without developer library/resource paths."""
import os
from pathlib import Path
import plistlib
import re
import subprocess
import sys
bundle=Path(sys.argv[1]).resolve()
with (bundle/'Contents/Info.plist').open('rb') as stream: info=plistlib.load(stream)
assert info['CFBundleExecutable']=='SingLilt'
assert info['NSMicrophoneUsageDescription']
assert (bundle/'Contents/Resources'/info['CFBundleIconFile']).is_file()
assert 'jpp' in info['CFBundleDocumentTypes'][0]['CFBundleTypeExtensions']
resources=bundle/'Contents/Resources'
for name in ('language/config.json','assets/lessons/01-scales.json','assets/soundfonts/Salamander.sf2','assets/soundfonts/GeneralUser-GS.sf2','licenses/LGPL-3.0-only.txt'):
    assert (resources/name).is_file(),name
assert (bundle/'Contents/PlugIns/platforms/libqcocoa.dylib').is_file()
binary=bundle/'Contents/MacOS/SingLilt'
for path in bundle.rglob('*'):
    if not path.is_file() or path.is_symlink():continue
    if path.suffix in ('.cpp','.h','.hpp','.mm','.ps1') or '.git' in path.parts:raise AssertionError('Source leaked: '+str(path))
    if path.name in ('SingLilt','crispembed') or path.suffix=='.dylib' or ('.framework' in str(path) and os.access(path,os.X_OK)):
        inspected=subprocess.run(['otool','-L',str(path)],capture_output=True,text=True,check=True).stdout
        for line in inspected.splitlines()[1:]:
            dep=line.strip().split(' (')[0]
            assert dep.startswith(('@','/System/Library/','/usr/lib/')), (path,dep)
        commands=subprocess.check_output(['otool','-l',str(path)],text=True)
        for rpath in re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset',commands):
            assert not rpath.startswith('/') or rpath.startswith(('/System/Library/','/usr/lib/')), (path,rpath)
subprocess.run(['codesign','--verify','--deep','--strict',str(bundle)],check=True)
environment=dict(os.environ)
for key in list(environment):
    if key.startswith(('QT_','DYLD_')) or key in ('QTDIR','JIANPU_SOUNDFONT','JIANPU_GM_SOUNDFONT'):environment.pop(key)
environment['PATH']='/usr/bin:/bin:/usr/sbin:/sbin'
for args in (['--version','--language','en_US'],['--catalog-check','--language','en_US']):
    result=subprocess.run([str(binary),*args],env=environment,cwd='/tmp',capture_output=True,text=True,timeout=30)
    assert result.returncode==0,(args,result.stderr)
print('PASS bundle metadata, resources, dependency closure, signatures and isolated startup')
