# Contributing

Small, focused changes are easier to review. Open an issue before starting a feature that changes the project format, playback model or recognition workflow.

## Build and test

Follow [BUILD.md](docs/BUILD.md). For domain-only changes:

```powershell
pwsh -NoProfile -File scripts/build.ps1 -CoreOnly -Configuration Debug
```

For desktop changes, build the app and run the affected CTest cases. Run the full local suite before a release. CI does not exercise physical microphones or MIDI devices.

## Code

- Match nearby code and `.clang-format`; do not reformat unrelated files.
- Keep ownership explicit and UI work on the GUI thread.
- Add a regression test for a bug fix. Do not change expected results merely to make a test pass.
- Keep domain rules independent of Qt. Application and UI code can use the existing domain functions.
- Comments explain constraints or decisions; avoid narrating each statement.

## Pull requests

Describe the behavior that changed, how you tested it, and any compatibility impact. Screenshots help for visual changes. Never include API keys, personal scores, recorded audio or files from `build`.

Project contributions use the MIT license. Include the source and license of any new third-party dependency.
