# macOS development and local distribution

SingLilt shares its C++20 domain, practice, persistence and Qt Widgets code with Windows and Linux. This branch adds shared asynchronous Qt Multimedia playback plus native decoding/capture, Apple Vision OCR, native OMR and a macOS application bundle. Linux support and its ALSA/Tesseract backends are preserved.

## Build

Install Xcode and select its developer directory. Install Homebrew, then run:

```sh
./scripts/setup-macos.sh
./scripts/build-macos.sh -DPython3_EXECUTABLE="$(brew --prefix python@3.12)/bin/python3.12"
./script/build_and_run.sh
```

The app is `build/bin/Release/SingLilt.app`. Core-only builds use `cmake --preset macos-core`, `cmake --build --preset macos-core-release`, and `ctest --preset macos-core-release`. The single `build` directory cannot contain Windows and macOS generator caches at the same time.

The current Homebrew-based desktop preset targets macOS 26 on Apple Silicon because the installed Qt Multimedia binary requires macOS 26. The architecture and dependency deployment targets must be rechecked before claiming Intel or older macOS support. Lower deployment targets require compatible builds of every library; changing only Info.plist is insufficient.

Piano and General MIDI banks and the native OMR model are downloaded with fixed SHA256 checks. The native OMR runtime is CrispEmbed v0.17.12 arm64, using Transcoda-59M-Q8, matching the Windows model. Optional Whisper lyric transcription and Python vocal separation are not bundled; explicit custom compatible helper paths remain available, and missing components are reported. No separate Python is required to launch the app or use core practice, OCR, OMR or playback.

## Deploy and package locally

```sh
./scripts/deploy-macos.sh
# Optional ZIP, when a local release artifact is wanted:
./scripts/package-macos.sh
```

Deployment copies Qt frameworks/plugins and non-system library dependencies, uses bundle-local resource paths, and applies ad hoc signatures. ZIP creation includes only the application bundle; code, the checkout and Git data are not uploaded. The scripts do not publish or push anything.

Ad hoc signing is local integrity signing, not Developer ID signing or Apple notarization. A downloaded copy can require explicit approval in System Settings > Privacy & Security > Open Anyway, if macOS offers that action. Do not disable Gatekeeper. No Apple Developer registration or App Store integration is used.

Microphone practice requires granting SingLilt microphone access. Denied access reports a settings instruction. Preferences and recovery data use writable user locations, never the bundle.

## LGPL libraries and rebuilding

Qt and FluidSynth are dynamically linked; retain the notices and license texts in `Contents/Resources/licenses`. Corresponding-source URLs and native build provenance are provided in `Contents/Resources/MACOS-DEPENDENCIES.md`. Recipients may modify and relink these libraries; a modified app must be ad hoc signed again after replacement, without requiring our private signing keys. Replace compatible libraries/frameworks inside `Contents/Frameworks`, preserve their install names, and re-sign nested code before the outer bundle. A library with an incompatible ABI must be used with a rebuilt application. This source project remains MIT; check every dependency's terms independently before public redistribution.

Tests involving Vision, Qt native media services or application launch must run with access to normal macOS system services. Restricted automation sandboxes can deny these services even when the application works outside them. Actual microphone permission/recording and older-system compatibility must be reported separately from generated audio/OCR regression checks.
