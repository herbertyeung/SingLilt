#!/usr/bin/env python3
# Whole-song arrangement and audio-import integration checks.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

"""A few real executable checks for complete arrangements and audio-import flows."""
from project_manifest import read_project
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

def main():
    root=Path(__file__).resolve().parent.parent
    parser=argparse.ArgumentParser()
    parser.add_argument('--executable',type=Path,default=root/'build/bin/Release/SingLilt.exe')
    parser.add_argument('--output',type=Path,default=root/'build/whole-song/workflow')
    parser.add_argument('--contract',action='store_true')
    parser.add_argument('--skip-ui',action='store_true')
    args=parser.parse_args(); output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    checks=[];runs=[]
    environment=os.environ.copy()
    environment['JIANPU_SOUNDFONT']=str(root/'build/bin/Release/assets/soundfonts/Salamander.sf2')
    environment.pop('JIANPU_GM_SOUNDFONT',None)
    def check(name,passed):
        checks.append({'name':name,'passed':bool(passed)})
        print(('PASS ' if passed else 'FAIL ')+name,flush=True)
    def run(name,options,expected=0):
        report=output/(name+'.json')
        command=[str(args.executable.resolve()),*map(str,options),'--report',str(report)]
        completed=subprocess.run(command,cwd=root,env=environment,capture_output=True,text=True,
            encoding='utf-8',errors='replace',timeout=150,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        runs.append({'command':subprocess.list2cmdline(command),'exit':completed.returncode,
                     'stdout':completed.stdout,'stderr':completed.stderr})
        if not args.contract:check(name+' exit',completed.returncode==expected)
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else {},completed.returncode
    intro=root/'build/private-fixtures/buxia-intro.jpp'
    report,status=run('candidates',['--whole-song-candidates','--inspect',intro,'--timeline'])
    if args.contract:
        count=len(report.get('wholeSongCandidates',[]))
        audio_import=False
        if status==0:
            imported,audio_status=run('audio-contract',['--transcribe-audio',root/'build/whole-song/integration/scale.wav',
                '--input-isolated-vocals','--audio-end','4.5','--audio-bpm','120','--out',output/'contract.jpp'])
            audio_import=audio_status==0 and imported.get('valid',False)
            if not audio_import:status=audio_status or 1
        print(f'SONG_CONTRACT candidates={count} audioImport={audio_import} exit={status}')
        (output/'runs.json').write_text(json.dumps(runs,indent=2),encoding='utf-8')
        return status
    candidates=report.get('wholeSongCandidates',[])
    check('three full arrangements',len(candidates)==3 and all(c['practice']['valid'] for c in candidates))
    signatures={json.dumps(c['practice']['events'],sort_keys=True) for c in candidates}
    check('genuinely different event schedules',len(signatures)==3)
    waves=[]
    for candidate in candidates:
        name=candidate['id']; path=output/(name+'.wav')
        rendered,_=run(name,['--generate-accompaniment','--accompaniment-variant',name,
            '--render-wave',path,'--render-seconds','8','--practice-mix','accompaniment',intro])
        check(name+' nonzero unclipped accompaniment',rendered.get('rms',0)>0 and rendered.get('clippedSamples')==0)
        waves.append((rendered.get('frames'),hashlib.sha256(path.read_bytes()).hexdigest()))
    check('same duration distinct audible tracks',len({w[0] for w in waves})==1 and len({w[1] for w in waves})==3)
    fixture=root/'build/whole-song/integration/scale.wav'
    imported,_=run('audio',['--transcribe-audio',fixture,'--input-isolated-vocals','--audio-end','4.5','--audio-bpm','120',
        '--audio-tonic','0','--out',output/'audio.jpp','--notation-image',output/'audio.png'])
    pitches=[e['midiPitch'] for e in imported.get('events',[]) if e['midiPitch']>=0 and e['attack']]
    check('known PCM pitch sequence',pitches==[60,62,64,65,67,69,71,72])
    check('known PCM ticks and absolute source mapping',sum(e['durationTicks'] for e in imported['events'])==4320 and
          len(imported['timings'])==len(imported['score']['notes']) and imported['timings'][0]['startSeconds']==0 and
          abs(imported['timings'][-1]['endSeconds']-4.5)<.01)
    reopened,_=run('roundtrip',['--inspect',output/'audio.jpp','--timeline'])
    check('audio project reopened with actual notation',reopened.get('generatedNotation') and
          reopened.get('audioSource',{}).get('mappingCurrent') and len(reopened.get('events',[]))==len(imported['events']))
    corrected=read_project(output/'audio.jpp', legacy_fixture=True)
    corrected['notes'][0]['octave']+=1
    (output/'pitch-corrected.jpp').write_text(json.dumps(corrected),encoding='utf-8')
    corrected_report,_=run('pitch-corrected',['--inspect',output/'pitch-corrected.jpp'])
    check('pitch correction preserves source-time mapping',corrected_report.get('audioSource',{}).get('mappingCurrent'))
    run('invalid-selection',['--transcribe-audio',fixture,'--input-isolated-vocals','--audio-start','2','--audio-end','1'],1)
    run('missing-source',['--transcribe-audio',output/'missing.mp3'],1)
    if not args.skip_ui:
        for name,options in [('whole-ui',['--whole-song-check']),('audio-ui',['--audio-task-check',fixture,'--input-isolated-vocals'])]:
            ui,_=run(name,options+['--screenshot',output/(name+'.png')])
            check(name+' actual controls/lifecycle',ui.get('passed'))
    result={'passed':all(c['passed'] for c in checks),'checks':checks,'runs':runs}
    (output/'summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(f'SONG_WORKFLOW {sum(c["passed"] for c in checks)}/{len(checks)} passed')
    return 0 if result['passed'] else 1

if __name__=='__main__':sys.exit(main())
