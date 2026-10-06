# Building SingLilt

## Requirements

The desktop app targets Windows 10/11 x64. The maintained local build uses Visual Studio 2026, the Desktop development with C++ workload, a Windows SDK, CMake 3.30+, PowerShell 7 and Python 3.12+.

`setup.ps1` downloads Qt 6.8.3, FluidSynth, the piano/GM sound banks and the native OMR runtime into `build/_deps`. Audio separation and lyric transcription are optional and require `-WithAnalysis`. Recognition itself does not download components.

```powershell
pwsh -NoProfile -File scripts/setup.ps1 -SkipBuild
pwsh -NoProfile -File scripts/build.ps1 -Configuration Release
```

Open `build/SingLilt.slnx` for the complete solution; `SingLilt.slnf` is an optional project filter. Headers are included in their target source lists. Do not create another build directory just to change configuration.

The application's build step deploys Qt DLLs and plugins with `windeployqt`, whether built from Visual Studio or the command line. Debug gets the `d` libraries and `platforms/qwindowsd.dll`; Release gets the release libraries. Run the executable from `build/bin/Debug` or `build/bin/Release` without adding the Qt SDK to PATH.

## Core tests

The core target uses the C++ standard library only. It can be built without Qt, SoundFonts or model downloads:

```powershell
pwsh -NoProfile -File scripts/build.ps1 -CoreOnly -Configuration Debug
```

On other systems, only this target is supported:

```sh
cmake -S . -B build -DSINGLILT_BUILD_APP=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

Return to the app with `cmake --preset vs2026`; that preset explicitly turns `SINGLILT_BUILD_APP` back on.

## Desktop checks

```powershell
ctest --test-dir build -C Release --output-on-failure
ctest --test-dir build -C Release -L ci --output-on-failure
ctest --test-dir build -C Release -R SingLiltMusicXml --output-on-failure
```

The `ci` label selects checks that do not require an audio device. Local checks also cover transport, rendering, persistence, recognition workflow and editing. Diagnostics store their settings and reports under `build`, separate from normal preferences.

Two legacy recognition regressions use a private score image. They are registered only when `build/private-fixtures/buxia.png` is present; `SINGLILT_PRIVATE_FIXTURES` can point to another local fixture directory. These files are not distributed. Keep this coverage distinction in test reports.

## Formatting

Run `clang-format -i` on the C++ files you changed. The repository uses Allman braces and four-space indentation. No broad formatting commit is needed for a small fix.
