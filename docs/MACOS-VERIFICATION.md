# macOS integration verification

Branch: `feature/macos-support`, based on the updated Windows/Linux `main` at `cae9d19`, version 0.27.0. This change retains Linux support and adds macOS support to the same C++20 application.

## Verification environment

Apple Silicon macOS 26, Qt 6.11.2, FluidSynth 2.6.1, Apple Clang, CMake and Ninja.
Application: `build/bin/Release/SingLilt.app`. Native OMR runtime and soundbanks are pinned and hash-verified by setup scripts.

## Verified results

- Final full macOS regression: **41/41 passed** in 50.30 seconds, including numbered-recognition protocol, source-selection, subprocess decoder, native audio/OCR, file-open and audio-cancellation checks.
- Original and relocated final bundles passed metadata, resource, dependency-path, strict signature and isolated startup checks.
- Relocated CLI tests: **7/7 passed**. Website regression: **4/4 passed**.
- Read-only review found no remaining Important/Critical integration issue. Windows/Linux native CI is enabled on the PR but its results are separate from local Mac verification.

## Integration scope

- Linux and macOS share asynchronous Qt Multimedia original-audio playback and POSIX process-group cancellation.
- macOS uses native Vision OCR, Qt capture/decoding, CoreAudio and bundled resource paths. Linux retains ALSA MIDI, Tesseract OCR and its subprocess decoder.
- Source selection checks exercise Windows, Linux and macOS without requiring foreign SDKs. The existing numbered-recognition and decoder-cancellation regressions remain enabled.
- Deployment includes Qt/native dependencies and notices and verifies ad hoc signatures, bundle-local loader paths and isolated startup.

## Remaining validation limits

Actual microphone permission grant/denial and hardware recording require manual validation; the lifecycle test does not create an active capture source. Windows and Linux compilation run in CI rather than on this Mac. Intel and older macOS are unverified; the current local Homebrew Qt Multimedia requires macOS 26. Finder fresh-session JPP association has not been checked. Optional Whisper and vocal separation are not bundled. Ad hoc signing is not Developer ID signing or notarization; this PR does not submit to the App Store or publish a Mac binary release.
