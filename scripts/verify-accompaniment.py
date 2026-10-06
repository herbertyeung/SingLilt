#!/usr/bin/env python3
# Arrangement, persistence, and audio-content integration checks.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

"""Focused real-executable practice, persistence, audio-content and UI checks."""

from project_manifest import read_project
import argparse
import array
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import wave


def main():
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parent.parent
    parser.add_argument('--executable', type=Path, default=root / 'build/bin/Release/SingLilt.exe')
    parser.add_argument('--output', type=Path, default=root / 'build/p0-singing/integration')
    parser.add_argument('--contract', action='store_true')
    parser.add_argument('--skip-ui', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    checks, runs = [], []
    env = os.environ.copy()
    env['JIANPU_SOUNDFONT'] = str(root / 'build/bin/Release/assets/soundfonts/Salamander.sf2')
    env.pop('JIANPU_GM_SOUNDFONT', None)

    def check(name, passed):
        checks.append({'name': name, 'passed': bool(passed)})
        print(('PASS ' if passed else 'FAIL ') + name, flush=True)

    def run(name, cli, expected=0):
        report = output / (name + '.json')
        command = [str(args.executable.resolve()), *map(str, cli), '--report', str(report)]
        completed = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True,
                                   encoding='utf-8', errors='replace', timeout=75,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        runs.append({'name': name, 'command': subprocess.list2cmdline(command),
                     'exit': completed.returncode, 'stdout': completed.stdout, 'stderr': completed.stderr})
        if not args.contract:
            check(name + ' exit', completed.returncode == expected)
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else {}, completed.returncode

    intro = root / 'build/private-fixtures/buxia-intro.jpp'
    generated = output / 'confirmed.jpp'
    report, status = run('generate', ['--generate-accompaniment', '--inspect', intro, '--timeline', '--out', generated])
    if args.contract:
        schema = read_project(generated)['schemaVersion'] if status == 0 else 0
        print(f'P0_CONTRACT generation={status == 0} schema={schema} exit={status}')
        (output / 'runs.json').write_text(json.dumps(runs, indent=2), encoding='utf-8')
        return status
    check('generated valid separate accompaniment', report.get('practice', {}).get('valid') and
          len(report['practice']['events']) > 0 and len(report['events']) == 47)
    packaged = read_project(generated)
    check('schema4 confirmed only', packaged['schemaVersion'] == 4 and 'accompaniment' in packaged and
          not any('candidate' in key.lower() for key in packaged))
    saved = read_project(generated, legacy_fixture=True)
    repeated, _ = run('deterministic', ['--generate-accompaniment', '--inspect', intro, '--timeline'])
    check('deterministic generation', repeated.get('practice') == report.get('practice'))
    legacy, _ = run('legacy', ['--inspect', intro, '--timeline'])
    check('legacy melody semantics unchanged', legacy.get('events') == report.get('events') and
          legacy.get('durationTicks') == report.get('durationTicks') and
          legacy.get('practice', {}).get('accompanimentEnabled') is False)

    waves = {}
    for mix in ('melody', 'accompaniment', 'both', 'silent'):
        path = output / (mix + '.wav')
        rendered, _ = run('wave-' + mix, ['--render-wave', path, '--render-seconds', '8',
                                         '--practice-mix', mix, generated])
        with wave.open(str(path), 'rb') as stream:
            pcm = stream.readframes(stream.getnframes())
            values = array.array('h', pcm)
            if sys.byteorder != 'little':
                values.byteswap()
            waves[mix] = {'frames': stream.getnframes(), 'hash': hashlib.sha256(pcm).hexdigest(),
                          'nonzero': any(values), 'peak': max(map(abs, values), default=0)}
            check(mix + ' wave format', stream.getframerate() == 48000 and stream.getnchannels() == 2 and
                  stream.getsampwidth() == 2 and len(pcm) == stream.getnframes() * 4)
        check(mix + ' clipping', rendered.get('clippedSamples') == 0)
    check('independent audible tracks', waves['melody']['nonzero'] and waves['accompaniment']['nonzero'] and
          waves['both']['nonzero'] and len({waves[key]['hash'] for key in waves}) == 4)
    check('silent zero signal same duration', not waves['silent']['nonzero'] and
          len({item['frames'] for item in waves.values()}) == 1)
    muted_without_arrangement = copy.deepcopy(saved)
    muted_without_arrangement.pop('accompaniment')
    muted_without_arrangement['practiceMix'].update(melodyEnabled=False, accompanimentEnabled=False)
    muted_path = output / 'saved-mute-no-arrangement.jpp'
    muted_path.write_text(json.dumps(muted_without_arrangement), encoding='utf-8')
    muted_report, _ = run('saved-mute', ['--render-wave', output / 'saved-mute.wav', '--render-seconds', '2', muted_path])
    check('saved mute used without CLI override', muted_report.get('rms') == 0)
    old, _ = run('unchanged-wave', ['--render-wave', output / 'legacy.wav', '--render-seconds', '8', intro])
    baseline_wave = root / 'build/p0-singing/baseline-melody.wav'
    if baseline_wave.exists():
        check('legacy WAV byte identical baseline', baseline_wave.read_bytes() == (output / 'legacy.wav').read_bytes())

    arpeggio, _ = run('arpeggio', ['--generate-accompaniment', '--accompaniment-pattern', 'arpeggio',
                                  '--inspect', intro, '--timeline'])
    check('pattern has distinct event schedule', arpeggio.get('practice', {}).get('events') != report['practice']['events'])
    fast, _ = run('transposed-fast', ['--render-wave', output / 'fast.wav', '--render-seconds', '30',
                                    '--practice-mix', 'both', '--transpose', '2', '--speed', '2', generated])
    expected_fast_frames = round(report['durationTicks'] * 48000 * 60 / (report['bpm'] * 480 * 2)) + 72000
    check('speed changes frames shared duration', abs(fast.get('frames', 0) - expected_fast_frames) <= 1 and
          fast.get('transpose') == 2 and fast.get('clippedSamples') == 0)
    _, _ = run('invalid-transpose', ['--render-wave', output / 'invalid.wav', '--transpose', '25', generated], 1)
    _, _ = run('fractional-transpose', ['--render-wave', output / 'invalid.wav', '--transpose', '1.5', generated], 1)

    custom, _ = run('generaluser', ['--render-wave', output / 'generaluser.wav', '--render-seconds', '3',
                                   '--gm-soundfont', root / 'build/_deps/SoundFonts/GeneralUser-GS/GeneralUser-GS.sf2',
                                   '--metronome', '--practice-mix', 'both', generated])
    check('explicit generaluser bank', 'GeneralUser-GS.sf2' in custom.get('gmSoundFontPath', '') and
          custom.get('rms', 0) > 0 and custom.get('clippedSamples') == 0)
    gm_fixture = copy.deepcopy(saved)
    gm_fixture['versePrograms'] = [4, 4]
    gm_project = output / 'electric-piano.jpp'
    gm_project.write_text(json.dumps(gm_fixture), encoding='utf-8')
    for name, options in (('system', []), ('generaluser', ['--gm-soundfont',
            root / 'build/_deps/SoundFonts/GeneralUser-GS/GeneralUser-GS.sf2'])):
        gm_report, _ = run('gm-' + name, ['--render-wave', output / ('gm-' + name + '.wav'),
                                       '--render-seconds', '3', '--practice-mix', 'melody', *options, gm_project])
        check(name + ' nonpiano rendering', gm_report.get('rms', 0) > 0 and gm_report.get('clippedSamples') == 0)
    check('GM banks produce distinct nonpiano audio', (output / 'gm-system.wav').read_bytes() !=
          (output / 'gm-generaluser.wav').read_bytes())
    _, _ = run('missing-gm', ['--render-wave', output / 'invalid.wav', '--gm-soundfont', output / 'missing.sf2', generated], 1)

    mutations = {
        'fractional-tick': lambda p: p['accompaniment']['chords'][0].update(startTick=0.5),
        'bad-root': lambda p: p['accompaniment']['chords'][0].update(rootPitchClass=12),
        'bad-pattern': lambda p: p['accompaniment']['settings'].update(pattern=99),
        'bad-volume': lambda p: p['practiceMix'].update(accompanimentVolume=2),
        'bad-boolean': lambda p: p['practiceMix'].update(melodyEnabled='false'),
        'overlap': lambda p: p['accompaniment']['chords'][1].update(startTick=0),
    }
    for name, mutate in mutations.items():
        invalid = copy.deepcopy(saved)
        mutate(invalid)
        path = output / (name + '.jpp')
        path.write_text(json.dumps(invalid), encoding='utf-8')
        run(name, ['--inspect', path], 1)
    stale = copy.deepcopy(saved)
    stale['notes'][1]['degree'] = 2
    path = output / 'stale.jpp'
    path.write_text(json.dumps(stale), encoding='utf-8')
    stale_report, _ = run('stale', ['--inspect', path, '--out', output / 'stale-resaved.jpp'])
    check('stale arrangement retained but invalid', stale_report.get('practice', {}).get('hasArrangement') and
          stale_report.get('practice', {}).get('valid') is False)
    stale_melody, _ = run('stale-melody', ['--render-wave', output / 'stale-melody.wav',
                                        '--practice-mix', 'melody', '--render-seconds', '2', path])
    check('stale arrangement does not block melody-only export', stale_melody.get('rms', 0) > 0)
    if not args.skip_ui:
        for backend in ('sampled', 'system'):
            ui, _ = run('ui-' + backend, ['--accompaniment-check', '--audio-backend', backend,
                                         '--screenshot', output / ('ui-' + backend + '.png')])
            check('actual UI and ' + backend + ' transport', ui.get('passed') and ui.get('screenshotSaved'))
        preview, _ = run('ui-preview', ['--accompaniment-preview-check', '--language', 'zh_CN',
                                       '--screenshot', output / 'ui-preview.png'])
        check('actual candidate review and stale-state UI', preview.get('passed'))
    summary = {'passed': all(item['passed'] for item in checks), 'checks': checks, 'runs': runs, 'waves': waves}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(f'ACCOMPANIMENT {sum(item["passed"] for item in checks)}/{len(checks)} passed')
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
