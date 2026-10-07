// Settings changes and optional-component validation checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "SettingsProductCheck.h"
#include "platform/RuntimePaths.h"
#include "recognition/AudioTranscriber.h"
#include "settings/AppSettings.h"
#include "settings/CapabilityStatus.h"
#include "ui/OptionsDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <stdexcept>

namespace singlilt
{
namespace
{
class SettingsProductProbe final : public QObject
{
  public:
    SettingsProductProbe(const QCommandLineParser &args, QApplication &app) : QObject(&app), args_(args), app_(app)
    {
        QTimer::singleShot(0, this, [this] { run(); });
    }

  private:
    void check(const QString &name, bool passed)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", passed}});
        passed_ &= passed;
    }

    template <typename T> T *field(OptionsDialog &dialog, const char *name)
    {
        auto *widget = dialog.findChild<T *>(name);
        if (!widget)
            throw std::runtime_error(QString("Missing product options field %1").arg(name).toStdString());
        return widget;
    }

    void writeFixture(const QString &path, const QByteArray &content = "fixture")
    {
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            throw std::runtime_error("Fixture directory creation failed");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size())
            throw std::runtime_error("Fixture file creation failed");
    }

    void checkCapabilities(const QString &folder)
    {
        const QByteArray oldPiano = qgetenv("JIANPU_SOUNDFONT");
        const auto restorePiano = qScopeGuard([oldPiano] { qputenv("JIANPU_SOUNDFONT", oldPiano); });
        qputenv("JIANPU_SOUNDFONT", (folder + "/assets/soundfonts/Salamander.sf2").toUtf8());
        AppSettings settings;
        settings.gmSoundFontPath = folder + "/gm.sf2";
        const auto missing = inspectCapabilities(settings, folder);
        check("Missing local component files are not reported as present",
              !missing.piano.filesPresent && !missing.gm.filesPresent && !missing.whisper.filesPresent &&
                  !missing.separation.filesPresent);
        writeFixture(folder + "/gm.sf2");
        writeFixture(folder + "/assets/soundfonts/Salamander.sf2");
        writeFixture(folder + "/tools/whisper/whisper-cli" + NativeExecutableSuffix);
        writeFixture(folder + "/models/ggml-base.bin");
        writeFixture(folder + SeparatorPythonPath);
        writeFixture(folder + "/tools/separation/separate_vocals.py");
        writeFixture(folder + "/tools/separation/models/955717e8-8726e21a.th");
        const QJsonObject metadata{{"schema", 1},
                                   {"name", "htdemucs"},
                                   {"signature", "955717e8"},
                                   {"file", "955717e8-8726e21a.th"},
                                   {"version", "demucs-infer-4.2.2"},
                                   {"sha256", "8726e21a993978c7ba086d3872e7608d7d5bfca646ca4aca459ffda844faa8b4"}};
        writeFixture(folder + "/tools/separation/models/model.json", QJsonDocument(metadata).toJson());
        const auto present = inspectCapabilities(settings, folder);
        check("File checks distinguish present files without running fake executables",
              present.piano.filesPresent && present.gm.filesPresent && present.whisper.filesPresent &&
                  present.separation.filesPresent);
        writeFixture(folder + "/tools/separation/models/model.json", "{}");
        const auto badMetadata = inspectCapabilities(settings, folder);
        check("Separation metadata version mismatch is visible",
              !badMetadata.separation.filesPresent && !badMetadata.separation.issues.isEmpty());
        AudioTranscriptionOptions options;
        options.separateVocals = false;
        options.recognizeLyrics = false;
        const QString emptyFolder = folder + "/no-components";
        check("Pitch-only import does not require separation or Whisper files",
              audioImportCapabilityError(options, emptyFolder).isEmpty());
        options.recognizeLyrics = true;
        check("Requested lyric recognition blocks before a missing process starts",
              !audioImportCapabilityError(options, emptyFolder).isEmpty());
        options.lyricsText = "provided lyrics";
        check("Provided lyrics bypass the optional Whisper prerequisite",
              audioImportCapabilityError(options, emptyFolder).isEmpty());
        options.separateVocals = true;
        check("Requested separation requires its actual worker files",
              !audioImportCapabilityError(options, emptyFolder).isEmpty());
        options.separateVocals = false;
        options.lyricsText.clear();
        options.whisperExecutable = folder + "/tools/whisper/whisper-cli" + NativeExecutableSuffix;
        options.whisperModel = folder + "/models/ggml-base.bin";
        check("Per-task custom Whisper paths are used by the preflight",
              audioImportCapabilityError(options, emptyFolder).isEmpty());
    }

    void checkOptions(const QString &folder)
    {
        AppSettings stale;
        stale.lessonDirectory = folder + "/removed-lessons";
        stale.gmSoundFontPath = folder + "/removed-gm.sf2";
        stale.whisperModel = folder + "/removed-model.bin";
        stale.vision.endpoint = "saved-invalid-endpoint";
        stale.separateVocals = false;
        stale.recognizeLyrics = false;
        OptionsContext original;
        original.score.bpm = 58.333333;
        original.mix.melodyVolume = 0.88885;
        AppSettings accepted = stale;
        OptionsContext acceptedContext = original;
        int commits = 0;
        OptionsDialog dialog(stale, original);
        dialog.applyChanges = [&](const AppSettings &settings, const OptionsContext &context)
        {
            saveAppSettings(settings, &accepted);
            accepted = settings;
            acceptedContext = context;
            ++commits;
            return true;
        };
        dialog.show();
        QApplication::processEvents();
        auto *buttons = field<QDialogButtonBox>(dialog, "optionsButtons");
        auto *apply = buttons->button(QDialogButtonBox::Apply);
        auto *language = field<QComboBox>(dialog, "optionsLanguage");
        language->setCurrentIndex(language->findData(stale.language == "en_US" ? "zh_CN" : "en_US"));
        apply->click();
        check("Unchanged missing optional paths do not block a language change", commits == 1);
        check("Apply retains exact unedited score and mix decimals",
              acceptedContext.score.bpm == original.score.bpm &&
                  acceptedContext.mix.melodyVolume == original.mix.melodyVolume);
        check("Apply leaves the options window open", dialog.isVisible());
        field<QDoubleSpinBox>(dialog, "optionsCurrentTempo")->setValue(92);
        apply->click();
        check("A current BPM change is independent of unchanged missing paths",
              commits == 2 && acceptedContext.score.bpm == 92);
        field<QLineEdit>(dialog, "optionsGmPath")->setText(folder + "/new-missing.sf2");
        field<QDoubleSpinBox>(dialog, "optionsCurrentTempo")->setValue(120);
        field<QTabWidget>(dialog, "optionsPages")->setCurrentIndex(4);
        apply->click();
        QApplication::processEvents();
        auto *error = field<QLabel>(dialog, "optionsValidationError");
        check("An edited invalid optional path rejects the entire candidate atomically",
              commits == 2 && acceptedContext.score.bpm == 92 &&
                  accepted.gmSoundFontPath == stale.gmSoundFontPath);
        check("Validation selects the owning page and shows a field-local error",
              field<QTabWidget>(dialog, "optionsPages")->currentIndex() == 1 && error->isVisible() &&
                  error->property("errorField").toString() == "optionsGmPath");
        check("Validation focuses the offending path",
              dialog.focusWidget() == field<QLineEdit>(dialog, "optionsGmPath"));
        field<QLineEdit>(dialog, "optionsGmPath")->setText(stale.gmSoundFontPath);
        field<QLineEdit>(dialog, "optionsWhisperModel")->setText(folder + "/new-missing.bin");
        apply->click();
        check("A second validation moves the error to the new field page",
              field<QTabWidget>(dialog, "optionsPages")->currentIndex() == 3 &&
                  error->property("errorField").toString() == "optionsWhisperModel" && commits == 2);
        field<QLineEdit>(dialog, "optionsWhisperModel")->setText(stale.whisperModel);
        field<QCheckBox>(dialog, "optionsRecognizeLyrics")->setChecked(true);
        apply->click();
        check("Enabling unavailable lyric recognition is rejected without saving",
              commits == 2 && !accepted.recognizeLyrics);
        dialog.reject();
        check("Cancel after Apply retains only previously accepted values",
              acceptedContext.score.bpm == 92 &&
                  QSettings().value("audio/gmSoundFontPath").toString() == stale.gmSoundFontPath);
        check("Local readiness labels expose the failed optional file state",
              !field<QLabel>(dialog, "optionsGmStatus")->property("filesPresent").toBool() &&
                  !field<QLabel>(dialog, "optionsWhisperStatus")->property("filesPresent").toBool());
    }

    void checkEnhancement()
    {
        for (bool separate : {false, true})
            for (bool enhance : {false, true})
            {
                AppSettings settings;
                settings.separateVocals = separate;
                settings.enhanceVoice = enhance;
                OptionsDialog dialog(settings, {});
                AppSettings accepted;
                int commits = 0;
                dialog.applyChanges = [&](const AppSettings &s, const OptionsContext &)
                {
                    accepted = s;
                    ++commits;
                    return true;
                };
                auto *enhancement = field<QCheckBox>(dialog, "optionsEnhanceVoice");
                check(QString("Enhancement initial separation=%1 enhancement=%2").arg(separate).arg(enhance),
                      enhancement->isEnabled() == separate && enhancement->isChecked() == (separate && enhance));
                field<QDialogButtonBox>(dialog, "optionsButtons")->button(QDialogButtonBox::Apply)->click();
                check(QString("Enhancement commit separation=%1 enhancement=%2").arg(separate).arg(enhance),
                      commits == 1 && accepted.enhanceVoice == (separate && enhance));
                field<QCheckBox>(dialog, "optionsSeparateVocals")->setChecked(false);
                check("Turning off separation disables and clears enhancement",
                      !enhancement->isEnabled() && !enhancement->isChecked());
            }
        QSettings().setValue("audio/separateVocals", false);
        QSettings().setValue("audio/enhanceVoice", true);
        check("Legacy inconsistent enhancement preferences normalize on load", !loadAppSettings().enhanceVoice);
    }

    void run()
    {
        try
        {
            QTemporaryDir temporary;
            if (!temporary.isValid())
                throw std::runtime_error("Temporary fixture directory creation failed");
            const auto piano = qgetenv("JIANPU_SOUNDFONT");
            const auto python = qgetenv("JIANPU_SEPARATOR_PYTHON");
            const auto restoreEnvironment = qScopeGuard(
                [&]
                {
                    if (piano.isNull())
                        qunsetenv("JIANPU_SOUNDFONT");
                    else
                        qputenv("JIANPU_SOUNDFONT", piano);
                    if (python.isNull())
                        qunsetenv("JIANPU_SEPARATOR_PYTHON");
                    else
                        qputenv("JIANPU_SEPARATOR_PYTHON", python);
                });
            qunsetenv("JIANPU_SOUNDFONT");
            qunsetenv("JIANPU_SEPARATOR_PYTHON");
            checkCapabilities(temporary.path());
            checkOptions(temporary.path());
            checkEnhancement();
        }
        catch (const std::exception &error)
        {
            check(QString::fromUtf8(error.what()), false);
        }
        QSaveFile report(args_.value("report"));
        const QByteArray content =
            QJsonDocument(QJsonObject{{"passed", passed_},
                                      {"checks", checks_},
                                      {"validation", "local files and bounded GUI; no inference"}})
                .toJson();
        const bool saved =
            report.open(QIODevice::WriteOnly) && report.write(content) == content.size() && report.commit();
        app_.exit(passed_ && saved ? 0 : 2);
    }

    const QCommandLineParser &args_;
    QApplication &app_;
    QJsonArray checks_;
    bool passed_ = true;
};
} // namespace

void runSettingsProductCheck(const QCommandLineParser &args, QApplication &app)
{
    new SettingsProductProbe(args, app);
}
} // namespace singlilt
