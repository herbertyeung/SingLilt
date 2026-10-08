# Changelog

## 0.27.0

- Add the Ubuntu 24.04 x64 desktop build and a relocatable Linux archive.
- Bundle the matching Qt runtime while keeping ALSA, FluidSynth, Tesseract and sound banks as system dependencies on Linux.
- Fix cancellation and source selection during asynchronous original-audio loading and preview.
- Publish Linux and Windows packages together from the release workflow.

## 0.26.0

- Keep CI language and project-package checks independent of audio hardware while retaining local playback checks.
- Add saved Light/Dark/Follow system appearance without changing score images or exports.
- Load additional interface language packs from the external language directory.
- Deploy the matching Qt runtime after direct Visual Studio Debug/Release builds.
- Remove the obsolete Java OMR backend; retain native staff recognition and MusicXML import.
- Preserve the current project when switching or auto-saving fails.
- Validate imported MusicXML candidates before replacing the project.
- Use consistent tempo, meter and duration limits for editing and persistence.
- Unify the project, executable, command-line and release names as SingLilt.
- Separate CLI and diagnostic code from startup; add core-only builds, CI workflows and installer packaging.
- Keep the selected original-image cursor visible when playback pauses.

## 0.25.0

- Compact the main window and give the score more space.
- Add direct practice/correction mode controls.
- Show inspector scrolling only when its content does not fit.

## 0.20.0–0.24.0

- Add, delete and edit original-image notes with undo/redo and recovery.
- Edit pitch, duration, voice and in-measure timing from the side panel.
- Estimate insertion timing from reliable same-system source anchors.
- Support same-beat chords and horizontal timing adjustments.

## 0.19.0

- Make native CrispEmbed/Transcoda OMR the default staff-image backend.
- Keep original pages and editable full-voice music together in JPP projects.

## 0.13.0–0.17.0

- Add MusicXML, multi-page staff notation, full-voice playback and original-image source anchors.
- Store page resources and corrected music in self-contained projects.

## 0.1.0–0.12.0

- Add numbered-notation recognition, repeats, lyrics and sampled piano.
- Add accompaniment, audio import, singing lessons and practice history.
- Add English/Chinese localization and portable packaging.
