#!/usr/bin/env python3
"""Remove developer-machine search paths from all deployed Mach-O objects."""
from pathlib import Path
import re
import subprocess
import sys
root=Path(sys.argv[1]).resolve()
magic={b'\xcf\xfa\xed\xfe',b'\xfe\xed\xfa\xcf',b'\xca\xfe\xba\xbe',b'\xbe\xba\xfe\xca'}
for path in root.rglob('*'):
    if not path.is_file() or path.is_symlink():continue
    with path.open('rb') as stream:
        if stream.read(4) not in magic:continue
    if path.suffix=='.dylib' and 'Frameworks' in path.parts:
        subprocess.run(['install_name_tool','-id','@rpath/'+path.name,str(path)],check=True)
    commands=subprocess.check_output(['otool','-l',str(path)],text=True)
    for rpath in re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset',commands):
        if rpath.startswith('/') and not rpath.startswith(('/usr/lib/','/System/Library/')):
            subprocess.run(['install_name_tool','-delete_rpath',rpath,str(path)],check=True)
