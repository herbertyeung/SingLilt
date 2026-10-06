# Releasing

## Before tagging

1. Review code, documentation, third-party notices and example files.
2. Build Release and run the local suite. Record any private-fixture or hardware coverage separately.
3. Confirm the version agrees in CMake, the application and the Windows resource file.
4. Create a portable package and installer, then test installation, upgrade and uninstall.
5. Review the Git diff before pushing.

The first public version has not been published. Hosted workflow execution still needs to be checked after the repository is pushed.

## Local artifacts

```powershell
pwsh -NoProfile -File scripts/setup-installer.ps1
pwsh -NoProfile -File scripts/package.ps1 -Zip
pwsh -NoProfile -File scripts/installer.ps1 -PackageDirectory build/releases/SingLilt-0.26.0-win-x64-core
pwsh -NoProfile -File scripts/source-archives.ps1
pwsh -NoProfile -File scripts/verify-installer.ps1 -Installer build/releases/SingLilt-0.26.0-win-x64-core-setup.exe
```

Use `-OutputDirectory` to put release artifacts on another drive. `package.ps1 -IncludeAnalysis` creates the full payload when all optional components have been deployed. ZIP and installer names identify version, architecture and edition.

The package manifest lists every payload file, its size and SHA256. Installer compilation validates those entries before reading the payload. Both archives and installers receive `.sha256` sidecars. An existing output is not silently replaced.

The release also includes corresponding Qt base, FluidSynth and libsndfile source archives. Their URLs and hashes are pinned in `packaging/third-party-sources.json`; publish this source ZIP alongside the binaries, not only the installer.

The installer uses a stable AppId and a per-user directory. It creates a Start menu entry and an optional desktop shortcut. Installation never runs setup/download scripts, and uninstall does not remove JPP files, preferences or practice history.

## GitHub Actions

`ci.yml` tests Debug/Release core and desktop builds, including standalone runtime startup, language switching and themes. `release.yml` runs the Release checks, builds ZIP/installer, verifies installation, and uploads the artifacts.

Push `v0.26.0` only when that version is ready. Tag builds create a **draft** GitHub Release. Only the publication job receives `contents: write`; PR builds are read-only. Review the draft artifacts before making it public.

Installers are not code-signed yet. Add signing only after a certificate and signing process have been chosen; do not store a certificate or password in the repository.
