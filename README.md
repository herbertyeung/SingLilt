# SingLilt

[English](#english) · [简体中文](#简体中文)

## English

SingLilt is a desktop app for Windows and Ubuntu that helps you play scores and practise singing. It is written in C++20 with Qt 6. Import a score, check the notes against the original, and save it as a JPP project. You can slow down playback, transpose the score, or repeat a difficult phrase.

[Project website](https://herbertyeung.github.io/SingLilt/)

### Features

- Read numbered notation and staff notation from images, or import MusicXML. Compare the result with the source image, browse multiple pages, and correct notes.
- Play back score parts with a sampled piano and 128 General MIDI instruments. Adjust tempo and transposition for practice.
- Repeat phrases, follow lyrics, work through singing lessons, and get microphone-based practice feedback.
- Save JPP projects, recover autosaved work, and export synthesized practice audio as WAV.
- Use Chinese or English, with Light, Dark, or Follow system appearance.
- Add interface translations under `language/<locale>` and restart the app; no rebuild is needed. See [language packs](docs/LANGUAGE_PACKS.md).

Image recognition produces an editable draft, not a checked score. Review complex rhythms and multiple voices before practising. Vocal separation and lyric transcription use optional local components that are not included in the core package.

### Install

Download the Windows x64 installer or portable ZIP, or the Ubuntu 24.04 x64 tar archive, from [Releases](https://github.com/herbertyeung/SingLilt/releases). For the Windows ZIP, extract it and run `SingLilt.exe`. For Ubuntu, install the [runtime packages](docs/LINUX.md#prebuilt-archive-ubuntu-2404-x64), extract the archive and run its `bin/singlilt` launcher.

The Windows core packages include the piano sound bank and native staff-recognition tools; they start without a separate Python installation. The Ubuntu archive includes the app and Qt, but uses distribution audio/OCR libraries and sound banks. Optional native OMR and transcription tools are not bundled on Ubuntu. Uninstalling leaves your projects and practice history in place.

Start with a built-in exercise, or import your own material, check it, and save it as a `.jpp` project.

[Quick start](docs/QUICK_START.md) · [Options](docs/OPTIONS.md) · [Staff notation](docs/STAFF_NOTATION.md) · [Troubleshooting](docs/TROUBLESHOOTING.md)

### Build from source on Windows

Install Visual Studio 2026 with the Desktop development with C++ workload, CMake 3.30+, PowerShell 7, and Python 3.12+.

```powershell
pwsh -NoProfile -File scripts/setup.ps1
```

Dependencies are downloaded to `build/_deps`. For subsequent builds:

```powershell
pwsh -NoProfile -File scripts/build.ps1 -Configuration Release
```

To build and run only the core tests, without Qt, sound banks, or models:

```powershell
pwsh -NoProfile -File scripts/build.ps1 -CoreOnly -Configuration Release
```

All configurations use `build`. Open `build/SingLilt.slnx` in Visual Studio 2026. Optional audio-analysis components can be installed with `scripts/setup.ps1 -WithAnalysis`. See [building SingLilt](docs/BUILD.md) for details.

For Ubuntu source builds and distribution packages, see [Linux development](docs/LINUX.md).

### Development and releases

[Architecture](docs/ARCHITECTURE.md) · [Contributing](CONTRIBUTING.md) · [Project format](docs/PROJECT_FORMAT.md) · [Release process](docs/RELEASING.md) · [Changelog](CHANGELOG.md)

CI builds and tests Windows and Ubuntu separately. A version tag builds the Windows installer/portable ZIP and Ubuntu tar archive with SHA256 files, then creates a draft release for review before publication.

### License

Project code is licensed under [MIT](LICENSE). Qt, sound banks, fonts, and models have their own terms; see [third-party notices](THIRD_PARTY_NOTICES.md). The bundled lessons and MusicXML examples are provided by this project. Personal songs and private test material are not distributed.

## 简体中文

SingLilt 把简谱、五线谱图片、MusicXML 和音频整理成可反复练习的材料。你可以听示范、慢速练习、循环难句，也可以校正识谱结果并保存工程，下次接着练。

[项目网站](https://herbertyeung.github.io/SingLilt/)

### 功能

- 图片识谱与 MusicXML 导入，支持原图对照、多页浏览和音符校正。
- 完整声部播放、钢琴采样音源、128 个 GM 乐器、速度与移调控制。
- 分句循环、歌词显示、基础唱歌课程和麦克风练习反馈。
- JPP 工程保存与自动恢复，WAV 音频导出。
- 中文和英文界面，浅色 / 深色 / 跟随系统主题。
- 外置语言包：新增 `language/<locale>` 后重启即可使用，见 [语言包说明](docs/LANGUAGE_PACKS.md)。

识谱结果是可编辑的初稿；复杂节奏和多声部作品需要核对。人声分离和歌词转录使用可选的本地组件，不包含在 core 安装包中。

### 安装

在 [Releases](https://github.com/herbertyeung/SingLilt/releases) 下载 Windows x64 安装程序、便携 ZIP，或 Ubuntu 24.04 x64 tar 包。Windows ZIP 解压后运行 `SingLilt.exe`；Ubuntu 版先安装[运行依赖](docs/LINUX.md#prebuilt-archive-ubuntu-2404-x64)，解压后运行 `bin/singlilt`。

Windows core 包含钢琴音源与本地五线谱识别组件，不依赖外部 Python 环境即可启动。Ubuntu tar 包包含程序和 Qt，但音频、OCR 库及音源由系统提供；本地 OMR 和转录工具为可选组件，不包含在包内。卸载不会删除你的工程或练习记录。

第一次打开可直接播放内置练习；使用自己的材料时，先导入，再核对，最后保存为 `.jpp`。

[快速入门](docs/QUICK_START.md) · [选项](docs/OPTIONS.md) · [五线谱](docs/STAFF_NOTATION.md) · [排障](docs/TROUBLESHOOTING.md)

### 从源码构建

需要 Visual Studio 2026 的桌面 C++ 工具、CMake 3.30+、PowerShell 7 和 Python 3.12+。

```powershell
pwsh -NoProfile -File scripts/setup.ps1
```

依赖下载到 `build/_deps`。后续增量构建：

```powershell
pwsh -NoProfile -File scripts/build.ps1 -Configuration Release
```

只运行不依赖 Qt、音源或模型的核心测试：

```powershell
pwsh -NoProfile -File scripts/build.ps1 -CoreOnly -Configuration Release
```

构建始终复用 `build`。可选音频分析组件用 `scripts/setup.ps1 -WithAnalysis` 安装。更多信息见 [构建说明](docs/BUILD.md)。

Linux 开发、后端差异与安装说明见 [Linux 构建说明](docs/LINUX.md)。

### 开发与发布

[架构](docs/ARCHITECTURE.md) · [贡献指南](CONTRIBUTING.md) · [工程格式](docs/PROJECT_FORMAT.md) · [发布流程](docs/RELEASING.md) · [版本记录](CHANGELOG.md)

PR 会分别运行 Windows 和 Ubuntu 测试。带版本号的 tag 会生成 Windows 安装程序与便携 ZIP、Ubuntu tar 包及 SHA256 文件，并创建草稿 Release；维护者检查后再发布。

### 许可

项目代码采用 [MIT](LICENSE)。Qt、音源、字体和模型遵循各自的许可，详见 [第三方说明](THIRD_PARTY_NOTICES.md)。内置课程与 MusicXML 示例由本项目提供；个人歌曲和开发测试素材不进入公开发行包。
