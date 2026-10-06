# Architecture

## Startup and commands

`src/main.cpp` initializes the SingLilt Qt identity, imports legacy preferences and records once, loads the language and starts either a command or the main window.

`i18n/LanguageManager` reads JSON catalogs from the executable's `language` directory. Default and fallback IDs come from `language/config.json`; immutable translator snapshots preserve existing concurrent-read behavior. Language packs are deployment data, not RCC inputs.

- `cli/CommandLine`: argument definitions and early command detection.
- `cli/ProjectCommands`: recognition, transcription, inspection and WAV export commands.
- `cli/JsonReport`: JSON report output with checked writes.
- `diagnostics/DiagnosticCommands`: diagnostic dispatch and isolated test settings.

The CLI uses the same domain, recognition and persistence code as the desktop app. It does not maintain a second music model.

## Music and practice

`domain` owns scores, written measures, expanded timelines, accompaniment and assessment rules. A source note and a playback occurrence are different concepts: repeats can play one written note more than once.

`application` contains staff correction and position/timing mapping. `practice` coordinates lesson sessions. `audio` owns transport, synthesis, microphone capture and pitch detection.

## Import, persistence and UI

`recognition` produces candidates and source anchors. Long-running recognition uses existing task objects and cancellation; candidates do not replace the current project until accepted.

`storage` reads and writes JPP containers, MusicXML, lessons and recovery snapshots. The UI edits the current `Project`, invokes these operations, and keeps unapplied drafts separate from saved state.

`ui` owns widgets and notation rendering. Playback cursors follow the same timeline used for sound. Original image pixels remain separate from editable musical events and source anchors.

`ui/ThemeManager` owns the application palette and shared widget stylesheet. The saved System/Light/Dark preference is separate from project content; Qt color-scheme notifications update System mode on the UI thread. Custom charts and canvas backgrounds use palette roles, while source images and exported notation keep their original colors.

## Build and release

CMake keeps target creation in the root file. `cmake/Sources.cmake` lists owned files, `Dependencies.cmake` resolves desktop libraries, `DeployRuntime.cmake` copies runtime resources, and `Tests.cmake` registers integration checks.

The portable package is a staged, hashed file set. The installer consumes that same set rather than independently selecting DLLs or models.
