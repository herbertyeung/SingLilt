#!/usr/bin/env python3
"""Collect actual native dependency versions, formula recipes and SBOMs."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
bundle=Path(sys.argv[1]).resolve()
prefix=Path(subprocess.check_output(['brew','--prefix'],text=True).strip())
installed={}
for item in (bundle/'Contents/Frameworks').iterdir():
    source=prefix/'lib'/item.name
    if not source.exists():continue
    relative=source.resolve().relative_to(prefix/'Cellar')
    formula,version=relative.parts[:2]
    installed[formula]=prefix/'Cellar'/formula/version
notices=bundle/'Contents/Resources/licenses/macos-runtime'
notices.mkdir(parents=True,exist_ok=True)
records=[]
for formula,root in sorted(installed.items()):
    dest=notices/formula
    dest.mkdir(exist_ok=True)
    for name in ('INSTALL_RECEIPT.json','sbom.spdx.json'):
        if (root/name).is_file():shutil.copy2(root/name,dest/name)
    for recipe in (root/'.brew').glob('*.rb'):shutil.copy2(recipe,dest/recipe.name)
    for sbom in (root/'share/qt/sbom').glob('*.spdx'):shutil.copy2(sbom,dest/sbom.name)
    for name in ('LICENSE','LICENSE.txt','COPYING','COPYING.LIB'):
        if (root/name).is_file():shutil.copy2(root/name,dest/name)
    records.append({'formula':formula,'version':root.name,'buildRecipe':f'https://github.com/Homebrew/homebrew-core','sourceAndLicenseInformation':'See included formula recipe and SBOM for exact upstream source and licence.'})
(notices/'DEPENDENCIES.json').write_text(json.dumps(records,indent=2)+'\n')
print('Recorded native dependency provenance:',len(records))
