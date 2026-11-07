# Numbered-score authoring workspace

The new native Qt workspace is used for newly authored numbered scores and their saved projects
(`editorMinimumBars > 0`). Imported image/staff material and legacy correction workflows retain
their existing controls. Authored multi-voice scores and the first notation-mark set are described below.

## Workflow

- File → New opens score setup. Cancelling leaves the current document intact. Creating a score
  still requires resolving unsaved changes and starts with four empty measures, 4/4, C, 120 BPM.
- The Notes / Measures / Text tabs group editing operations. The right panel switches between
  score properties and detailed note properties. The keyboard and property panel can collapse.
- Focus the score and type 0–7, or click the piano. An existing selection changes pitch;
  an empty slot enters a note and advances. Escape / End / Resume entry returns to the end.
- Duration buttons select entry duration or change a selected note. Dots toggle for supported
  durations. Accidentals, octave changes and ties apply to selection; entry pitch settings persist.
- Click Add four measures to extend the blank layout. Insert/delete measure operate on the
  selected note's measure or the selected empty slot. Skipping time before note entry writes
  explicit rests; trailing empty measures never create fake playback events.
- Lyrics start at the selected note and skip rests. `(words)` assigns one syllable, `%` leaves a
  blank, `_` draws an extension. Excess syllables disable Apply. Existing later lyrics and other
  verses are preserved. Lyrics, metadata and structure participate in undo/redo.

## Boundaries and ownership

`NumberedEditing` owns uniform-meter melody structure and the 128-measure editor limit.
Structural changes reject repeats, written-meter maps and timed key changes rather than corrupting
their indices. Insertions inside sustained notes split them; deletion trims overlapping durations.
`NotationRenderer` produces the score image, note anchors and optional input-slot geometry.
`ScoreView` owns only hit testing and transient overlays. MainWindow coordinates the existing
project, undo stack and playback engine on the GUI thread. Components use parent-owned widgets;
no new workers, audio engines or persistence backends were introduced.

Score metadata adds optional `subtitle`, `lyricist`, `composer` and `editorMinimumBars` fields.
Missing fields keep legacy defaults. Input is validated before mutating the live score.
The JPP envelope and save path are unchanged. Authored voices and marks use schema 7; shared measure structure uses schema 8. See [project format](PROJECT_FORMAT.md).

## Validation and test environment

`SingLiltNumberedEditor` exercises the real new-score dialog, buttons, keyboard events, lyrics,
history, persistence, empty-score roundtrip and 1320×940 / 980×700 screenshots.
`SingLiltCore` covers blank measures, skipped beats, crossing-note splits, insert/delete timing,
structural guards and limits. Project-package diagnostics now accept the new setup dialog and use
numeric note entry instead of the superseded blank-page double-click workflow.

On Windows, windeployqt normally deploys only `qwindows.dll`. The editor test explicitly selects
the Qt installation's offscreen plugin and Windows font directory. `QT_QPA_PLATFORM_PLUGIN_PATH`
must be a single directory, not a semicolon-joined PATH-style list. Normal application startup
continues to use the Windows plugin; no shell associations or persistent environment variables change.

Transaction artifacts and run-specific evidence are under `build/editor-transaction`.
Rollback checks run in an isolated source copy; they do not reset the working feature branch.

## Document header redesign

The workspace now has a single `WorkspaceHeader`: score title, save state, native compact
New/Save/Undo/Redo tools, a More menu, and an Edit/Preview segment. File/history controls share
exactly the QAction instances owned by the main menu; there is no second shortcut or undo state.
The title is elided while its full text remains in the tooltip. The save-state tooltip keeps the
project path. Classroom/ear-training/navigation remain in the Practice menu rather than a second
row of buttons. Legacy staff-specific tools are still available on their contextual row.

`NumberedEditorToolbar` owns only notation categories, category tools, input-position actions and
keyboard/property visibility toggles. Panel toggles sit in the tab row corner. Preview hides notation
input tools without returning to the previous navigation rows, and keeps document commands available.
Switching to preview still resolves note drafts and restores Edit when cancellation is requested.

Validation covers light/dark rendering, minimum window size, long-title elision, shared shortcuts,
preview input suppression and panel preference preservation. `StaffSmartNoteCheck` accepts the new
score-setup dialog before testing discard/recovery failures; its existing recovery assertions remain.

For an open, unsaved application session, build a separate executable in the same build tree:

```powershell
cmake --build build --config Release --target SingLilt --parallel 4 -- /p:TargetName=SingLiltPreview
```

This leaves `build/bin/Release/SingLilt.exe` untouched and creates `SingLiltPreview.exe` beside it.
The live session is neither closed nor restarted. The next ordinary build (without the override)
returns to the normal application target name. Header-only rollback evidence is recorded separately
in `build/header-transaction` and restores the preceding, uncommitted authoring implementation.

## Main score view zoom

The canvas-bottom zoom bar exposes minus/plus, an editable percentage, actual size (100%) and
fit width. Ctrl + wheel zooms around the pointer; pixel-delta trackpads and zero-delta events are
handled explicitly. With a score view focused, Ctrl + plus/minus/0 provides keyboard zoom.
Typing in metadata/lyric fields does not invoke these view-local shortcuts.

Zoom uses the QGraphicsView transform as its single scale value, bounded to 15–500%. Manual zoom
is retained across edits, undo/redo, panel resizing, original/recognized view switching and
Edit/Preview. Each view keeps its own ratio. Fit width resumes automatic fitting after viewport
resizes. Loading a new project initially fits both views; deferred layout work respects a newer
manual zoom request. Zoom never changes document data or dirty state and is not persisted to JPP.

Build this iteration without replacing either prior executable:

```powershell
cmake --build build --config Release --target SingLilt --parallel 4 -- /p:TargetName=SingLiltZoomPreview
```

Transaction evidence is in `build/zoom-transaction`.


## Authored voices and performance marks

The Voices tab adds whole-score voices (up to eight), selects, renames, reorders and deletes them.
Deleting a nonempty voice confirms first. IDs remain stable across renames/reordering. Clicking
another voice's note or empty slot selects that voice. New voices start at the first beat rather
than inheriting the previous voice's input cursor. Each voice owns its notes and lyrics; a single
shared instrument uses the existing program-A control. All voices share key, meter and tempo.

`NumberedComposition` is authoritative. `Project.score.notes` is only the active-view projection;
legacy note-inspector edits enter the same validated composition transaction. Saving writes the
first ordered voice as the compatibility guide, independently of which voice is selected. Voice
selection is not a document edit. Undo/redo captures the complete composition and selected voice,
including activation from a legacy one-voice score. Whole-measure insertion/deletion edits all
voices at equal written times. Short or empty voices never stretch another voice's rhythm.

Rendering measures all voices, shares bar widths and onset positions, then stacks voices per
system. Hit identities carry voice and note indexes and work through the existing Zoom transform.
The renderer checks the combined raster budget before allocating all voice images. Existing limits
remain: 128 measures, 100,000 total notes, 16,000-pixel rendered height; oversized edits fail before
replacing the current score. This version does not add pagination or section-scoped voices.

Marks have defined sounding semantics, not decorative-only buttons:
- Dynamics inherit within each voice. pp/p/mp/mf/f/ff map to MIDI velocities 32/48/64/80/96/112;
  an unmarked voice begins at the score's base velocity.
- Accent adds 16, capped at 127, to the attack velocity.
- Staccato halves the sounding gate, never the written duration or the next onset. A tied chain
  sounds once; only a staccato on its terminal note shortens the terminal gate.

The composition compiler feeds the existing polyphonic event scheduler with one common program.
A temporary guide is padded only for playback, so all voices remain audible beyond the active
voice's last note. GUI and CLI WAV exports use this same plan. No additional audio backend or
worker thread was introduced. Existing staff-image/import workflows remain separate.

Composition/mark projects without shared measure structure use JPP schema 7. Legacy projects still load unchanged; conversion happens
on the first voice/mark edit. Schema 7 validates every voice, stable IDs, names, note fields and the
canonical guide. Older executables reject this version rather than silently losing extra voices.
Per-voice instruments, ornamentation and region-scoped voice management are not included.

Build: `cmake --build build --config Release --target SingLilt --parallel 4 -- /p:TargetName=SingLiltVoicesPreview`.
Run-specific source snapshots, regression reports, screenshots, offline WAV and rollback evidence
are under `build/voices-transaction`.

## Review regressions and rendering changes

The review regressions were run against the pre-fix code first. The UI suite reported 26 failing
checks out of 33; the core deletion-tie regression also failed. The same assertions pass after the
fixes. Both platforms register the authoring and review diagnostics with CTest.

Metadata changes now validate the complete authored score before publishing. Repeat operations
are rejected at the command boundary as well as disabled in the menu/inspector. Playback rebuild
contains unexpected validation failures, stops playback and reports the error. Empty scores are
an editing state rather than an audio-load error. Canvas selections return an explicit acceptance
result so Cancel preserves drafts and stops the action after one prompt.

Shared measure layout merges rhythmic onsets with measured glyph/lyric extents. It no longer uses
an equal-cell estimate to draw proportionally spaced short notes. Empty slots stay inside measure
boundaries; a supported wide empty meter adapts its slot widths. Deleted measure cursors snap to
remaining slots, while a filled final measure exposes a separate append cursor. Removing time
preserves existing ties between surviving adjacent notes, without inventing a new tie across a
removed successor.

Measurement is now a non-rasterizing layout pass. Validated images/layouts are reused on commit;
value comparisons replace JSON serialization in the no-op path. Diagnostic raster counters measured
a two-voice edit going from 10 to 3 rasterizations and voice selection from 5 to 0. These are counts,
not claims about peak resident memory. Selection updates guide/overlays without restarting audio;
voice-name emphasis is transient and is not baked into the persisted score image.

Language changes refresh position/append labels, key choices and overlay text without resetting
input drafts. Schema 7 and the existing playback backend remain unchanged.
