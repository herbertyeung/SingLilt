# Linux development

Linux desktop support uses the shared Qt Widgets application, core domain, file formats
and settings. Windows keeps its existing platform implementations. The separate Mac port
can select its own source files without depending on Linux ALSA or Tesseract.

## Dependencies (Ubuntu 24.04, x86-64)

```bash
sudo apt-get update
sudo apt-get install -y build-essential python3-venv ninja-build pkg-config \
  libasound2-dev libfluidsynth-dev libtesseract-dev \
  tesseract-ocr-eng tesseract-ocr-chi-sim fluid-soundfont-gm ffmpeg \
  fonts-dejavu-core libgl1-mesa-dev libegl1 libopengl0 libx11-xcb1 \
  libxcb-cursor0 libxkbcommon-x11-0 libxcb-xinerama0 libxcb-icccm4 \
  libxcb-keysyms1 libxcb-image0 libxcb-render-util0 libxcb-randr0 \
  libxcb-shape0 libxcb-shm0 libxcb-sync1 libxcb-xfixes0 libxcb-xkb1
bash scripts/setup-linux.sh
bash scripts/build-linux.sh Release
build/bin/Release/SingLilt
```

Qt 6.8.3 (including ICU, Qt Declarative and Multimedia) is downloaded into
`build/_deps/Qt`. The supplied FFmpeg media plugin also links Qt Quick/QML libraries,
even though the application uses Widgets. Setup checks the actual plugin dependencies
with `ldd` before building. Distro Qt versions older than 6.8 do
not satisfy the existing theme APIs. You can instead supply your own Qt >= 6.8
(including Multimedia and Core private headers) via `CMAKE_PREFIX_PATH`. Development
libraries are discovered through pkg-config and CMake's ALSA package.

Core-only builds need CMake >= 3.30, Ninja and a C++20 compiler, not Qt or sound banks:

```bash
bash scripts/build-linux.sh Debug core
```

All generated files stay in `build`. Use a separate source checkout on Linux if the
Windows checkout already contains a Visual Studio build; do not share a CMake cache
between incompatible hosts/generators. Debug and Release use the same multi-config tree.

## Runtime behavior

- SoundFont playback is the default. FluidSynth selects PulseAudio (including PipeWire's
  PulseAudio compatibility server) or ALSA from its compiled drivers. A driver-open
  failure is reported rather than silently changing the selected backend.
- A bundled Salamander piano takes priority. If absent, Linux uses the distribution GM
  bank (`FluidR3_GM.sf2`, `default-GM.sf2`, or `default.sf2`) for piano as well. This is a
  different sound than the Windows sampled piano. `JIANPU_SOUNDFONT` and
  `JIANPU_GM_SOUNDFONT` retain their existing override semantics.
- Backend integer 1 / `--audio-backend system` uses an actual ALSA MIDI destination.
  Run a MIDI synthesizer or attach a MIDI device first; Linux has no built-in Windows
  MIDI Mapper equivalent. No endpoint is reported as an error.
- Microphone recording uses ALSA capture hints, mono PCM16 at 48 kHz, a worker-owned
  nonblocking device, bounded block queue and monotonic timestamps. ALSA's default PCM
  should route to the user's desktop audio server.
- Original audio uses Qt Multimedia for load, seek, pause, rate and volume. FFmpeg and
  ffprobe on PATH decode import selections to 16 kHz mono PCM. Sources remain limited
  to 20 minutes and each decoded interval to 120 seconds, with cancellation/deadlines.
- OCR uses Tesseract with `eng` or `chi_sim+eng` language data and original-image word
  rectangles. Missing language data is an explicit failure.
- Optional tools use `tools/whisper/whisper-cli`,
  `tools/separation/python/bin/python3`, and `tools/omr-native/crispembed`; model layout
  and validation are unchanged. They are not installed by the Linux setup script.
  Supply compatible tools/models explicitly when using those features. The native OMR
  engine and its workers share a private process group for timeout/cancellation cleanup.

## Prebuilt archive (Ubuntu 24.04 x64)

The GitHub Release archive `SingLilt-0.27.0-ubuntu-24.04-x64.tar.gz` contains the app and its Qt runtime. It uses Ubuntu's ALSA, FluidSynth, Tesseract and other system libraries. Install the runtime packages, then extract the archive without rearranging its `bin`, `lib` and `share` directories:

```bash
sudo apt-get update
sudo apt-get install -y libasound2t64 libfluidsynth3 libtesseract5 \
  tesseract-ocr-eng tesseract-ocr-chi-sim fluid-soundfont-gm ffmpeg \
  fonts-dejavu-core libgl1 libegl1 libopengl0 libx11-xcb1 libxcb-cursor0 libxkbcommon-x11-0 \
  libxcb-xinerama0 libxcb-icccm4 libxcb-keysyms1 libxcb-image0 libxcb-render-util0 \
  libxcb-randr0 libxcb-shape0 libxcb-shm0 libxcb-sync1 libxcb-xfixes0 libxcb-xkb1
mkdir -p "$HOME/Applications"
tar -xzf SingLilt-0.27.0-ubuntu-24.04-x64.tar.gz -C "$HOME/Applications"
"$HOME/Applications/singlilt-0.27.0-Linux/bin/singlilt"
```

The desktop package is tested with X11/XWayland; it is a tar archive, not a `.deb` or AppImage. Other distributions need their own compatible system libraries or a source build.

## Build from source and package locally

```bash
build/tools/linux-env/bin/cmake --install build --config Release --prefix "$HOME/.local"
"$HOME/.local/bin/singlilt" --version --language en_US
build/tools/linux-env/bin/cpack --config build/CPackConfig.cmake -C Release -B build/packages
```

The wrapper in `bin` launches the executable/data in `lib/singlilt` (or the configured
GNUInstallDirs lib directory). The `.desktop` entry and icon are installed under `share`.
The TGZ includes replaceable Qt libraries, ICU/media dependencies and plugins under
`lib/singlilt/qt`, selected by Qt's deployment API. The executable uses an `$ORIGIN`-relative
RPATH and `qt.conf`, so the development checkout can be moved or removed after installation.
This is not an AppImage: ALSA, FluidSynth, Tesseract, desktop system libraries and sound banks
remain distribution-managed runtime dependencies. The launcher resolves symlinks before
locating the installed executable.

## Validation

The Linux workflow builds Debug and Release, runs headless CLI/storage/renderer/process
checks and Linux media regressions, and tests the installed wrapper/catalog, symlink invocation, dependency origins and an
extracted archive after the development Qt directory has been hidden. Media tests
exercise real FFmpeg decode, offline FluidSynth rendering and Tesseract extraction without
requiring speakers or a microphone. Test physical output, device switching, microphone
latency and desktop interaction on a real Linux desktop before publishing a release.

Reusable implementation/review prompt:

```text
Continue feature/linux-port. Read .planning/linux-port/task_plan.md and docs/LINUX.md.
Keep Windows backends and the Mac port independent. Reuse build and existing interfaces.
Run scripts/build-linux.sh Debug and Release on Linux; report actual build/test results,
then verify installed startup, playback, microphone capture, import cancellation and OCR.
Do not call Windows-only test success a Linux-native verification.
```
