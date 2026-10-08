# Third-party notices

SingLilt uses dynamically linked Qt 6.8.3 (Core, GUI, Widgets, Network and Concurrent; Linux also deploys Multimedia, QML and Quick), Copyright The Qt Company Ltd. and other contributors. Qt's applicable open-source terms include LGPL-3.0 / GPL-3.0 and module-specific notices. The libraries remain separate, replaceable shared files.

- Source for the corresponding version: https://download.qt.io/archive/qt/6.8/6.8.3/submodules/
- Qt licensing: https://www.qt.io/licensing/open-source-lgpl-obligations
- Qt third-party licenses: https://doc.qt.io/qt-6.8/licenses-used-in-qt.html
- Bundled Qt library SBOM: build/_deps/Qt/6.8.3/msvc2022_64/sbom/

Windows OCR, Windows MIDI and their instrument samples are provided by the user's Windows installation; they are not redistributed by this project.

## Sampled piano (0.3.0)

- **Salamander Grand Piano v3**, sampled by **Alexander Holm**; SF2 assembly by **Roberto, FreePats project**. Licensed under **Creative Commons Attribution 3.0 Unported**. The SF2 bank is unmodified; source archive and attribution: https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html . Credits and upstream format limitations are retained in `licenses/salamander/`.
- **FluidSynth 2.6.1**, dynamically linked, unmodified official Windows x64 binary; LGPL terms, authors, and source/release links are retained in `licenses/fluidsynth/`.
- The official runtime includes **SDL3 3.2.10** and **libsndfile 1.2.2**; their supplied licenses and upstream build provenance are retained alongside FluidSynth's notices.
- `gm.dls` is read from the local Windows system directory at runtime to preserve other GM instruments and percussion. It is not bundled, copied, or modified by the setup/build/package scripts.
- Sample archive SHA256: `15EDB061D7BA60D58332F72DBA8F8CE40988048CC703F935E6320F37D650E213`.
- Unmodified SF2 SHA256: `712D0E681EFBE5203A8014E9B3E84168F1908C82F2F6FB13BD2C77D6D72C70B7`.

Public examples use project-authored lessons and MusicXML scores. Personal score images, song audio and private recognition fixtures are not distributed.

aqtinstall 3.3.0 is a build-time download tool only (MIT license); its Python dependencies stay under build/tools and are not runtime dependencies.

## Optional General MIDI bank (0.6.0)
- GeneralUser GS 2.0.3, S. Christian Collins. The author's complete license, including contained-sample notes, is retained in licenses/generaluser-gs/LICENSE.txt.
- Source: https://github.com/mrbumpy409/GeneralUser-GS at commit 684543d5e5efaef08d02be50dcda8d552478fa60.
- Unmodified SF2 SHA256: 9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe.
- This bank is an explicit alternative to the local Windows GM bank. It does not replace the dedicated Salamander piano or redistribute Windows gm.dls.

## Staff notation font (0.13.0)
- Bravura, Steinberg Media Technologies GmbH, unmodified OpenType font embedded in application resources.
- Source: https://github.com/steinbergmedia/bravura, redist/otf/Bravura.otf.
- SIL Open Font License1.1 retained in licenses/Bravura-OFL.txt; font registration is per process, not system installation.

## Local audio analysis (0.7.0)
- Audio decoding uses Windows Media Foundation; original playback uses Windows MCI. No system audio samples or source songs are redistributed.
- whisper.cpp1.8.3 official Windows x64 CPU runtime, MIT license, dynamically separate command-line process. Source: https://github.com/ggml-org/whisper.cpp/tree/v1.8.3 ; notices in licenses/whisper/.
- Multilingual Whisper base weights (147951465bytes), corresponding OpenAI Whisper MIT terms in licenses/whisper/MODEL-LICENSE.txt. Model SHA25660ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe.
- Runtime archive SHA256d824b1e37599f882b396e73f1ee0bfd5d0529f700314c48311dcbd00b803321d; fixed URLs and hash checks are in scripts/setup-audio.ps1. Audio and lyric inference are local; no paid/cloud request is needed.
- Local generated scale WAV and Windows SAPI speech are integration fixtures, not recorded singing reference material.

## Native OMR (0.19.0 default runtime)

CrispEmbed v0.17.12 Windows CPU CLI and ggml libraries are unmodified, MIT.
Source commit: 78493a31fc3043f3af4ab6fd2f197a32da6f07f0.
Release: https://github.com/CrispStrobe/CrispEmbed/releases/tag/v0.17.12
Transcoda-59M-Q8 GGUF weights (68,599,232 bytes) are CC-BY-4.0.
Original model: btrkeks/transcoda-59M-zeroshot-v1; GGUF conversion by CrispStrobe/cstr.
Locked revision: c4ed06a104f30d19503aa2ef8f79a23152167faa.
Model: https://huggingface.co/cstr/transcoda-omr-GGUF
Model SHA256: a6a977676339a7add577aa95089c2d30fb912b185ef117b068fe826e91a8a283.
Code licenses, model card, CC-BY terms and attribution accompany tools/omr-native/licenses.
The reference model project's code is not included in this runtime.

## Windows installer

The installer uses Inno Setup 6.7.3 by Jordan Russell and Martijn Laan. Its license is retained in `licenses/inno-setup/LICENSE.txt`; corresponding compiler/engine source is available at https://github.com/jrsoftware/issrc/tree/is-6_7_3 . The build tool is not installed on the user's machine.

## Linux desktop runtime

Linux packages include dynamically linked, replaceable Qt 6.8.3 libraries and the media
plugin's Qt/ICU/FFmpeg dependencies selected by Qt's deployment tooling. Loader paths may
be adjusted for relocation; application runtime data stay separate from these libraries.
The release source ZIP includes Qt Base, Declarative and Multimedia 6.8.3, FFmpeg 7.1 and
ICU 73.2 sources, alongside the existing FluidSynth and libsndfile sources. FFmpeg's
deployed libraries identify LGPL 2.1 or later; its license text and ICU's license are
included under `licenses/ffmpeg` and `licenses/icu`. Qt LGPL/GPL terms are retained under
the package's Qt license directory.

ALSA and Tesseract are distribution-managed libraries, not copied into the TGZ.
- ALSA source: https://github.com/alsa-project/alsa-lib ; terms: https://github.com/alsa-project/alsa-lib/blob/master/COPYING .
- Tesseract source: https://github.com/tesseract-ocr/tesseract ; terms: https://github.com/tesseract-ocr/tesseract/blob/main/LICENSE .
- FFmpeg source/licensing: https://ffmpeg.org/download.html and https://ffmpeg.org/legal.html .

Distribution GM banks remain at their installed system paths and are not redistributed
in the Linux TGZ. Optional recognition tools/models retain their own existing terms.
