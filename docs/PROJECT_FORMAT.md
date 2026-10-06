# Portable project format (.jpp), schemas 4, 5 and 6

## User workflow
- File → New (Ctrl+N): blank numbered-score canvas; double-click to insert notes or import an image/audio from File.
- File → Open project (Ctrl+O): open a .jpp. Image and audio import remain separate actions.
- File → Save (Ctrl+S) / Save As (Ctrl+Shift+S): save score, applied note corrections, source rectangles, lyrics, repeats/ties, A/B instruments, confirmed accompaniment, mix, transposition/speed, metronome, original-audio speed/volume/source and practice loop.
- Before replacing an edited document, Save / Discard / Cancel preserves the user's choice. Cancelled file dialogs keep the document dirty. Saving does not trigger AI or re-run transcription.
- A saved project embeds its notation PNG, original MP3/WAV and any actual vocals/instrumental MP3/WAV resources. Only used files are included. Processed stems currently produced by separation are WAV, not an invented MP3 export.
- Move a schema4 .jpp alone to another machine with SingLilt 0.11.0 or later; optional full-staff fields require the versions described below. Schema5 multi-page projects require SingLilt 0.15.0 or later. Resource extraction uses an owned temporary directory; original source/model paths are not needed for playback. Audio hardware, synthesizer and globally selected SoundFonts remain machine-specific.
- Existing JSON schema 1/2/3 projects open unchanged. An in-place save upgrades to the binary container (schema4, or schema5 when page assets are added) and retains a byte-identical `<name>.jpp.legacy.bak` (numbered if a different backup already exists). Save As leaves the old file untouched.
- Missing audio prevents saving a partial project and preserves the previous file. Restore the external file for a legacy/imported project, then save again. A loaded version4 project already owns extracted audio.

## Container specification
All lengths count bytes, not characters. Header: 8-byte magic `JPP4\r\n\x1a\n`, 4-byte big-endian unsigned JSON length, UTF-8 JSON manifest. Payloads immediately follow in manifest resource order; no alignment or compression.

Manifest `schemaVersion=4` contains existing score/mix/accompaniment fields, optional `practiceSettings`, `processing`, `imageAsset=asset:image`, and `resources`. Each resource has a fixed role (`image`, `original`, `vocals`, `instrumental`), display-only filename, extension, decimal-string `size`, lowercase SHA256. Audio metadata `path`, `vocalsPath`, `instrumentalPath` refer exclusively to their corresponding `asset:<role>`; absent stems use empty strings. Timings and fingerprints remain source-time based.

Manifest `schemaVersion=5` adds `staffPages` for a single song containing independent physical pages. Each page records `label`, half-open unexpanded `startTick` / `endTick`, `sourceAsset` and `renderedAsset`. Page0 reuses `asset:image`; later rendered pages use `asset:page-score-N` (N=1..31), and available source-page PNG snapshots use `asset:page-source-N` (N=0..31). A missing source image has an empty `sourceAsset`, not an invented scan. Pages are contiguous in song time and must agree with the authoritative written-measure page ranges.

The magic remains `JPP4\r\n\x1a\n`; the manifest schema selects page support, not a different envelope. A multi-page save uses schema5, while projects without page assets retain schema4. Version0.15.0 reads schema4 and existing schema1/2/3 documents; older applications may reject schema5. Code rollback does not downgrade a newly saved schema5 file.

Schema4 limits: manifest16MiB, 1–4 resources, image64MiB / 50 million pixels / 12000×20000 bounds, each audio2GiB, total7GiB. Counts, roles, references, lengths, image bounds and checksums must pass before accepting a Project. The loader constructs extraction paths only from fixed roles and validated extensions; metadata filenames never become extraction paths. Copied Projects share the cache lifetime, which ends with the last owner; native audio is closed before MainWindow releases its project.

Schema5 supports at most32 pages and67 resources, including optional audio. Individual image limits stay unchanged; decoded source and rendered images together are limited to400 million pixels. Page roles must be referenced by their own metadata entry; orphan, missing or mismatched page resources are rejected. Each note's positive source rectangle is checked against its own rendered page, not the dimensions of page0.

ProjectStore validates domain/editor/practice metadata. ProjectPackage hashes and streams resources in bounded chunks and commits through QSaveFile. A failed write or integrity check does not publish partial content. The container stores the resources actually used; it does not record speculative AI results, API keys, default host settings or unconfirmed accompaniment drafts.

New transcriptions record algorithm, selection parameters, suggested tempo/key, lyric model/language and separation model/version/hash/configuration from the actual separation manifest. Old projects retain only processing facts they originally had; absent provenance is not fabricated. Resource SHA256 is refreshed from the actual embedded bytes at each save.

## Verification

### Optional staff fields (0.13.0)

`notationView` selects `numbered` or `staff` and stores the clef/key display settings. Pitched notes may carry `staffSpelling` (step, alter, octave) so enharmonic spelling survives saving.

`staffPerformance` preserves every written pitched event, including the guide voice, with a shared start/duration clock, staff/voice identity, velocity, sound ties and source rectangle. The guide Score only drives transport/practice selection; it does not add a duplicate audio channel. Loading validates timing fingerprint, complete guide coverage, bounded fields and closed sound ties, except for explicitly marked unresolved review chains described below. Original image projects retain their image; generated MusicXML projects retain a synchronized grand-staff image. These optional fields require 0.13.0 for full-staff playback; absence keeps the existing numbered-score behavior. See [staff notation](STAFF_NOTATION.md).

### Written measures, page coordinates and review flags (0.15.0)

- `Score.writtenMeasures` stores `{startTick,durationTicks,number,beatsPerBar,beatUnit,pageIndex}`. Spans cover the unexpanded source timeline continuously; local meters determine the real measure length and metronome downbeats. Numeric MusicXML labels use printed number minus1. Only an initial short pickup may use number=-1 to display printed measure0.
- Guide notes and complete `staffPerformance.notes` store zero-based `pageIndex`. Their rectangles are local to that rendered page. The guide remains a practice line; every original sounding event, including simultaneous chord tones, stays in the complete performance.
- `staffPerformance.clefChanges` stores `{startTick,staff,bassClef}` for standard G/F clefs, including mid-measure changes. A display-clef change does not transpose stored MIDI pitches. `Timeline.metronomeBeats` is derived from the expanded source order and local meters rather than saved as a second timing authority.
- Optional `staffPerformance.notes[].unresolvedSoundTie` explicitly marks a local-OMR review chain. Its original `tieStart` / `tieStop`, pitch and timing remain stored. Those marked events audition separately at their original durations; no missing note, longer hold or guessed connection is inserted. Ordinary MusicXML import stays strict. Persisted review flags and warnings remain visible after reopening and do not imply corrected notation or measured recognition accuracy.

### Project container checks

`SingLiltProjectPackage` runs through the application target: move with originals removed, audio hash equality/re-save/cache lifetime, legacy upgrade/backup, corruption/truncation/oversize/reference rejection, failed-save preservation, and native File actions/dialogs/viewport editing. Default PCM fixtures exercise three WAV sources; `--project-test-audio` additionally exercises an existing real MP3 copied into the isolated test fixture. These checks validate persistence and playback opening, not transcription or acoustic singing accuracy.

The normal executable remains `build/bin/Release/SingLilt.exe`; diagnostic arguments are not required for normal use.

### Staff instruments (0.14.0)

Optional staffPerformance.primaryProgram/otherProgram store zero-based GM programs0..127 for the primary/other staff. Absence reads as piano0. Programs are performance parameters, not part of the timing fingerprint; changing them preserves every written pitch, tick and velocity. They route to channels1/2 in real-time playback, audition and WAV export. Legacy numbered-score versePrograms retain their old meaning.

### Fidelity metadata and manual correction (0.16.0)

Staff performance notes optionally retain `beams` (levels1..8, explicit begin/continue/end/forward-hook/backward-hook) and `stemDirection` (auto/up/down/none). Absence preserves legacy rendering; invalid types, states and duplicate beam levels are rejected. These fields do not replace authoritative MIDI pitch or tick durations.

All new local OMR imports, including one-page clipboard material, store `staffPages` source and rendered PNG assets. `processing.tempoNeedsConfirmation`, an optional conservative `tempoSuggestion`, and explicit `tempoSource`/`confirmedQuarterBpm` distinguish missing imported tempo from the user's checked value. Original MusicXML/source hashes remain provenance, not proof of musical accuracy.

User correction records retain original diagnostics and mark `manualStaffCorrection.accuracyMeasured=false`. Explicit edits rebuild complete voices, their guide, written-measure clock, page ranges and generated anchors as one validated Project copy. Source pixels are unchanged. Validated note changes are undoable; cancelling leaves the original document intact. Saved full-performance edits affect playback and export, not only the picture.

### Original-image playback (0.17.0 / schema6)

The JPP4 binary envelope is unchanged; schema6 explicitly requires `staffImagePlayback=true` and `generatedNotation=false`. Each staff page requires its original source image, and its compatible `renderedImage`/display resource contains the same original pixels, not a newly generated score. Fixed resource roles remain supported; duplicate roles may refer to equal PNG snapshots. No Renderer participates in local recognition or source-mode correction.

Every source-mode guide/performance note explicitly stores boolean `hasImageAnchor`. True requires a finite positive box strictly inside its own source page. False requires all four bbox coordinates to be zero. Coordinates come from same-run SIG head geometry or an explicit user-specified image position; neither pitch-based estimated Y nor legacy regenerated boxes are treated as source positions. Old schemas1..5 keep their previous coordinate meaning until an explicit UI source migration clears incompatible anchors.

Schema6 persists shared sound timing, voices/instruments, original pixel resources, page-qualified source positions and missing-position states. Source image equality and authoritative written page time ranges are validated on both save and load. Localization, speed/transposition, correction and repeated playback do not modify source PNG pixels. Position coverage is separately recorded from musical recognition accuracy.

### Smart note edits and recovery (0.21.0)

Explicit user edits may snap a source box to a measured staff-line pitch. They are user corrections, not new OMR detections. Optional processing.staffVisualEdits records page/staff/voice/startTick/midiPitch/source identity, originalSource, five measured staffLines, spacing and accidental alter. The renderer draws only uniquely matching current events. These display hints never replace authoritative performance pitch/timing or modify source images; older software may omit the red overlay while retaining the music.

Independent recovery packages store processing.staffEditRecovery with version1, sessionId, sourcePath, sourceSHA256, sourceHashStatus, recoveryPath and updatedAtUtc. A snapshot includes the complete ordinary JPP payload and commits atomically. Recovery does not overwrite the source project or mark its edits clean; reopening through the recovery action requires a formal Save As. Undo/Redo also schedules the current snapshot, including return to a clean baseline.

### Timing suggestion metadata (0.23.0)

Optional processing.staffVisualEdits[].timingSource is distance, chord or manual. It describes the user's committed new-note timing suggestion or an explicit onset/duration edit; authoritative ticks remain in staffPerformance. Automatic suggestions and old unconfirmed added notes do not bootstrap the spacing scale. Original recognized or explicitly manual-timed anchors can calibrate local spacing; same-beat alignment can reuse a new distance/chord column. This metadata does not certify OMR accuracy or silently correct existing music.
