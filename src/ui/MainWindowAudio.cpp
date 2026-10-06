// Audio import, stem analysis, and original-audio practice.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MainWindow.h"
#include "NotationRenderer.h"
#include "RecognitionPreviewDialog.h"
#include "ScoreView.h"
#include "i18n/LanguageManager.h"
#include "settings/CapabilityStatus.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QTextBoundaryFinder>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>

namespace singlilt
{
namespace
{
QString inputPath(const AudioSourceInfo &source, int selection)
{
    if (selection == 1)
        return QString::fromStdString(source.path);
    if (!source.vocalsSeparated)
        return {};
    if (selection == 2)
        return QString::fromStdString(source.vocalsPath);
    if (selection == 3)
        return QString::fromStdString(source.instrumentalPath);
    return {};
}
bool mappingMatchesInput(const Project &project, const OriginalAudioPlayer &audio, int selection)
{
    if (!project.audioSource || project.audioSource->timings.empty() || !audio.isOpen())
        return false;
    const QString expected = inputPath(*project.audioSource, selection);
    return !expected.isEmpty() &&
           QFileInfo(audio.sourcePath()).absoluteFilePath() == QFileInfo(expected).absoluteFilePath() &&
           project.audioSource->timingFingerprint == audioTimingFingerprint(project.score);
}
// Dialog-owned playback never replaces the accepted project's original-audio device.
class PreviewAudioSession final : public QObject
{
  public:
    PreviewAudioSession(MainWindow &window, std::function<bool()> intent, std::function<void(bool)> setIntent,
                        std::function<void(const QString &)> reportError, QDialog *dialog)
        : QObject(dialog), window_(window), intent_(std::move(intent)), setIntent_(std::move(setIntent)),
          reportError_(std::move(reportError))
    {
        stateTimer_.setInterval(50);
        connect(&stateTimer_, &QTimer::timeout, this,
                [this]
                {
                    if (!active_)
                        return;
                    setProperty("auditionPlaying", audio_.isPlaying());
                    setProperty("auditionPositionSeconds", audio_.positionSeconds());
                });
        connect(dialog, &QDialog::finished, this, [this] { finish(); });
        for (const char *name : {"play", "stop"})
            if (auto *button = window.findChild<QPushButton *>(name))
                connect(button, &QPushButton::clicked, this, [this] { abandon(); });
        if (auto *source = window.findChild<QComboBox *>("playbackSource"))
            connect(source, &QComboBox::currentIndexChanged, this, [this] { abandon(); });
        if (auto *volume = window.findChild<QSlider *>("originalVolume"))
            connect(volume, &QSlider::valueChanged, this,
                    [this](int value)
                    {
                        if (active_ && !audio_.setVolume(value / 100.0))
                            reportError_(audio_.errorString());
                    });
        if (auto *speed = window.findChild<QDoubleSpinBox *>("originalSpeed"))
            connect(speed, &QDoubleSpinBox::valueChanged, this,
                    [this](double value)
                    {
                        if (active_ && !audio_.setSpeed(value))
                            reportError_(audio_.errorString());
                    });
    }
    OriginalAudioPlayer &audio()
    {
        return audio_;
    }
    QString errorString() const
    {
        return error_;
    }
    bool play(const QString &path, double startSeconds)
    {
        error_.clear();
        if (!audio_.open(path))
        {
            error_ = audio_.errorString();
            return false;
        }
        if (!active_ || !acceptedTransportUnchanged())
        {
            wasMelodyPlaying_ = window_.player().isPlaying();
            wasOriginalPlaying_ = window_.originalAudioPlayer().isPlaying();
            previousIntent_ = intent_();
            const auto *source = window_.findChild<QComboBox *>("playbackSource");
            selection_ = source ? source->currentData().toInt() : 0;
            fingerprint_ = audioTimingFingerprint(window_.project().score);
            imageIdentity_ = window_.project().image.cacheKey();
            projectSourcePath_ = window_.project().audioSource
                                     ? QString::fromStdString(window_.project().audioSource->path)
                                     : QString();
            originalPath_ = window_.originalAudioPlayer().sourcePath();
        }
        window_.player().pause();
        if (window_.originalAudioPlayer().isOpen())
            window_.originalAudioPlayer().pause();
        pausedTick_ = window_.player().positionTicks();
        pausedSeconds_ = window_.originalAudioPlayer().positionSeconds();
        setIntent_(false);
        active_ = true;
        const auto *speed = window_.findChild<QDoubleSpinBox *>("originalSpeed");
        const auto *volume = window_.findChild<QSlider *>("originalVolume");
        const bool ok = audio_.setSpeed(speed ? speed->value() : 1.0) &&
                        audio_.setVolume(volume ? volume->value() / 100.0 : .9) && audio_.seek(startSeconds) &&
                        audio_.play();
        setProperty("auditionSourcePath", audio_.sourcePath());
        setProperty("auditionPlaying", ok && audio_.isPlaying());
        setProperty("auditionPositionSeconds", audio_.positionSeconds());
        if (ok)
            stateTimer_.start();
        if (!ok)
        {
            error_ = audio_.errorString();
            finish();
        }
        return ok;
    }
    void finish()
    {
        stateTimer_.stop();
        audio_.close();
        setProperty("auditionPlaying", false);
        setProperty("auditionPositionSeconds", 0.0);
        if (active_ && acceptedTransportUnchanged())
        {
            bool restored = true;
            if (wasOriginalPlaying_)
                restored = window_.originalAudioPlayer().play();
            else if (wasMelodyPlaying_)
                restored = window_.player().play();
            setIntent_(restored && previousIntent_);
            if (!restored)
            {
                const QString restoreError = wasOriginalPlaying_ ? window_.originalAudioPlayer().errorString()
                                                                 : window_.player().errorString();
                error_ = error_.isEmpty() ? restoreError : error_ + '\n' + restoreError;
                reportError_(error_);
            }
        }
        active_ = false;
    }
    void abandon()
    {
        stateTimer_.stop();
        audio_.close();
        setProperty("auditionPlaying", false);
        setProperty("auditionPositionSeconds", 0.0);
        active_ = false;
    }

  private:
    bool acceptedTransportUnchanged() const
    {
        const auto *source = window_.findChild<QComboBox *>("playbackSource");
        return !window_.player().isPlaying() && !window_.originalAudioPlayer().isPlaying() &&
               (!source || source->currentData().toInt() == selection_) &&
               audioTimingFingerprint(window_.project().score) == fingerprint_ &&
               window_.project().image.cacheKey() == imageIdentity_ &&
               (window_.project().audioSource ? QString::fromStdString(window_.project().audioSource->path)
                                              : QString()) == projectSourcePath_ &&
               window_.originalAudioPlayer().sourcePath() == originalPath_ &&
               window_.player().positionTicks() == pausedTick_ &&
               std::abs(window_.originalAudioPlayer().positionSeconds() - pausedSeconds_) < .03;
    }
    MainWindow &window_;
    OriginalAudioPlayer audio_;
    QTimer stateTimer_;
    std::function<bool()> intent_;
    std::function<void(bool)> setIntent_;
    std::function<void(const QString &)> reportError_;
    std::string fingerprint_;
    QString originalPath_;
    QString projectSourcePath_;
    QString error_;
    qint64 imageIdentity_ = 0;
    std::int64_t pausedTick_ = 0;
    double pausedSeconds_ = 0;
    int selection_ = 0;
    bool active_ = false, wasMelodyPlaying_ = false, wasOriginalPlaying_ = false, previousIntent_ = false;
};

double sourceSecondsAtTick(const AudioSourceInfo &source, std::int64_t tick)
{
    for (const auto &timing : source.timings)
    {
        if (tick >= timing.startTick && tick < timing.endTick)
            return timing.startSeconds + (timing.endSeconds - timing.startSeconds) *
                                             double(tick - timing.startTick) /
                                             double(timing.endTick - timing.startTick);
    }
    return tick <= 0 ? source.selectedStartSeconds : source.selectedEndSeconds;
}

std::int64_t sourceTickAtSeconds(const AudioSourceInfo &source, double seconds)
{
    for (const auto &timing : source.timings)
    {
        if (seconds >= timing.startSeconds && seconds < timing.endSeconds)
            return timing.startTick + std::int64_t(std::llround(double(timing.endTick - timing.startTick) *
                                                                (seconds - timing.startSeconds) /
                                                                (timing.endSeconds - timing.startSeconds)));
    }
    if (source.timings.empty() || seconds < source.selectedStartSeconds)
        return 0;
    return source.timings.back().endTick;
}

Project transcriptionProject(const AudioTranscriptionResult &result, const AudioTranscriptionOptions &options)
{
    Project project;
    project.score = result.score;
    project.image = renderNumberedScore(project.score);
    project.warnings = result.warnings;
    project.generatedNotation = true;
    AudioSourceInfo source;
    source.path = result.sourcePath.toStdString();
    source.durationSeconds = result.sourceDurationSeconds;
    source.selectedStartSeconds = result.selectedStartSeconds;
    source.selectedEndSeconds = result.selectedEndSeconds;
    source.timings = result.timings;
    source.lyricTimings = result.lyricTimings;
    source.timingFingerprint = audioTimingFingerprint(project.score);
    source.vocalsPath = result.vocalsPath.toStdString();
    source.instrumentalPath = result.instrumentalPath.toStdString();
    source.separationModel = result.separationModel.toStdString();
    source.vocalsSeparated = result.vocalsSeparated;
    project.audioSource = std::move(source);
    project.processing = audioProcessingMetadata(result, options);
    return project;
}
} // namespace

void MainWindow::createAudioControls(QVBoxLayout *layout)
{
    auto *row = new QHBoxLayout;
    auto *import = button("ui.audio_import.import", row);
    import->setObjectName("importAudio");
    auto *lyrics = button("ui.audio_import.edit_lyrics", row);
    lyrics->setObjectName("editSongLyrics");
    playbackSource_ = new QComboBox;
    playbackSource_->setObjectName("playbackSource");
    addTranslatedItem(playbackSource_, "ui.audio_import.synthesized", 0);
    addTranslatedItem(playbackSource_, "ui.audio_import.original", 1);
    addTranslatedItem(playbackSource_, "ui.vocal_separation.vocals_source", 2);
    addTranslatedItem(playbackSource_, "ui.vocal_separation.instrumental_source", 3);
    playbackSource_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    row->addWidget(playbackSource_);
    row->addWidget(label("ui.audio_import.original_speed"));
    auto *originalSpeed = new QDoubleSpinBox;
    originalSpeed->setObjectName("originalSpeed");
    originalSpeed->setRange(0.25, 2.0);
    originalSpeed->setSingleStep(0.1);
    originalSpeed->setValue(1.0);
    originalSpeed->setMaximumWidth(80);
    row->addWidget(originalSpeed);
    row->addWidget(label("ui.audio_import.original_volume"));
    auto *originalVolume = new QSlider(Qt::Horizontal);
    originalVolume->setObjectName("originalVolume");
    originalVolume->setRange(0, 100);
    originalVolume->setValue(90);
    originalVolume->setMaximumWidth(80);
    row->addWidget(originalVolume);
    auto *sourceSettings = new QWidget(centralWidget());
    sourceSettings->setObjectName("legacySourceSettings");
    sourceSettings->setLayout(row);
    sourceSettings->hide();
    auto *taskControls = new QWidget;
    taskControls->setObjectName("audioTaskControls");
    auto *taskRow = new QHBoxLayout(taskControls);
    taskRow->setContentsMargins(0, 0, 0, 0);
    audioTaskStatus_ = new QLabel(taskControls);
    audioTaskStatus_->setObjectName("audioTaskStatus");
    audioTaskStatus_->setTextFormat(Qt::PlainText);
    audioTaskStatus_->setWordWrap(true);
    taskRow->addWidget(audioTaskStatus_, 1);
    audioPreviewButton_ = button("ui.audio_import.preview", taskRow);
    audioPreviewButton_->setObjectName("audioTaskPreview");
    audioCancelButton_ = button("ui.audio_import.cancel", taskRow);
    audioCancelButton_->setObjectName("audioTaskCancel");
    audioDiscardButton_ = button("ui.audio_import.discard", taskRow);
    audioDiscardButton_->setObjectName("audioTaskDiscard");
    layout->addWidget(taskControls);
    connect(import, &QPushButton::clicked, this,
            [this]
            {
                const QString path = QFileDialog::getOpenFileName(this, trText("ui.audio_import.import"), {},
                                                                  trText("ui.audio_import.filter"));
                if (!path.isEmpty())
                    openAudioImport(path);
            });
    connect(lyrics, &QPushButton::clicked, this, [this] { editLyrics(); });
    connect(playbackSource_, &QComboBox::currentIndexChanged, this, [this] { changePlaybackSource(); });
    connect(originalSpeed, &QDoubleSpinBox::valueChanged, this,
            [this, originalSpeed](double speed)
            {
                if (originalAudio_.isOpen() && !originalAudio_.setSpeed(speed))
                {
                    setStatusMessage(originalAudio_.errorString());
                    QSignalBlocker blocker(originalSpeed);
                    originalSpeed->setValue(originalAudio_.speed());
                }
            });
    connect(originalVolume, &QSlider::valueChanged, this,
            [this](int volume)
            {
                if (!originalAudio_.setVolume(volume / 100.0))
                    setStatusMessage(originalAudio_.errorString());
            });
    connect(audioPreviewButton_, &QPushButton::clicked, this, [this] { previewAudioResult(); });
    connect(audioCancelButton_, &QPushButton::clicked, this, [this] { audioTask_.cancel(); });
    connect(audioDiscardButton_, &QPushButton::clicked, this, [this] { discardAudioResult(); });
    audioTask_.stateChanged = [this]
    {
        if (audioTask_.state() != AudioTranscriptionTask::State::Ready)
            audioCandidate_.reset();
        refreshAudioTaskUi();
        if (audioTask_.state() == AudioTranscriptionTask::State::Ready ||
            audioTask_.state() == AudioTranscriptionTask::State::Failed)
            QApplication::alert(this, 3000);
    };
    audioTask_.progress = [this](int percentage, const QString &stage)
    { audioTaskStatus_->setText(trText("ui.audio_import.progress").arg(percentage).arg(localizeMessage(stage))); };
    refreshAudioTaskUi();
}

void MainWindow::openAudioImport(const QString &path)
{
    if (busy_ || audioLoading_ || audioTask_.isRunning() || audioTask_.result())
    {
        setStatus("ui.audio_import.finish_pending");
        return;
    }
    if (audioImportDialog_)
    {
        audioImportDialog_->raise();
        return;
    }
    auto *dialog = new QDialog(this);
    auto *audition = new PreviewAudioSession(
        *this, [this] { return playIntent_; }, [this](bool playing) { playIntent_ = playing; },
        [this](const QString &error) { setStatusMessage(error); }, dialog);
    audition->setObjectName("audioImportAudition");
    if (!audition->audio().open(path))
    {
        setStatusMessage(audition->audio().errorString());
        dialog->deleteLater();
        return;
    }
    audioImportDialog_ = dialog;
    dialog->setObjectName("audioImportDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    bindText(dialog, "ui.audio_import.title", "windowTitle");
    dialog->resize(640, 560);
    auto *layout = new QVBoxLayout(dialog);
    auto *source = new QLabel(QFileInfo(path).absoluteFilePath(), dialog);
    source->setTextFormat(Qt::PlainText);
    source->setWordWrap(true);
    layout->addWidget(source);
    auto *help = label("ui.vocal_separation.import_help");
    help->setWordWrap(true);
    layout->addWidget(help);
    auto *form = new QFormLayout;
    auto *start = new QDoubleSpinBox(dialog);
    start->setObjectName("audioStartSeconds");
    start->setDecimals(2);
    start->setRange(0, std::max(0.0, audition->audio().durationSeconds() - 0.1));
    auto *end = new QDoubleSpinBox(dialog);
    end->setObjectName("audioEndSeconds");
    end->setDecimals(2);
    end->setRange(0.1, audition->audio().durationSeconds());
    end->setValue(std::min(60.0, audition->audio().durationSeconds()));
    auto *bpm = new QDoubleSpinBox(dialog);
    bpm->setObjectName("audioSuggestedBpm");
    bpm->setRange(0, 400);
    bpm->setValue(0);
    bindText(bpm, "ui.audio_import.automatic", "specialValueText");
    auto *recognize = checkBox("ui.audio_import.recognize_lyrics");
    recognize->setObjectName("audioRecognizeLyrics");
    recognize->setChecked(settings_.recognizeLyrics);
    auto *separate = checkBox("ui.vocal_separation.separate");
    separate->setObjectName("audioSeparateVocals");
    separate->setChecked(settings_.separateVocals);
    auto *enhance = checkBox("ui.vocal_separation.enhance");
    enhance->setObjectName("audioEnhanceVoice");
    enhance->setChecked(settings_.enhanceVoice && settings_.separateVocals);
    enhance->setEnabled(settings_.separateVocals);
    connect(separate, &QCheckBox::toggled, dialog,
            [enhance](bool enabled)
            {
                enhance->setEnabled(enabled);
                if (!enabled)
                    enhance->setChecked(false);
            });
    auto *wholeSong = checkBox("ui.audio_import.whole_song");
    wholeSong->setObjectName("audioWholeSong");
    wholeSong->setChecked(true);
    start->setEnabled(false);
    end->setEnabled(false);
    connect(wholeSong, &QCheckBox::toggled, dialog,
            [start, end](bool whole)
            {
                start->setEnabled(!whole);
                end->setEnabled(!whole);
            });
    form->addRow(label("ui.audio_import.start"), start);
    form->addRow(label("ui.audio_import.end"), end);
    form->addRow(label("ui.audio_import.bpm"), bpm);
    auto *language = new QComboBox(dialog);
    language->setObjectName("audioLyricLanguage");
    addTranslatedItem(language, "ui.audio_import.automatic", "auto");
    addTranslatedItem(language, "ui.language.zh_CN", "zh");
    addTranslatedItem(language, "ui.language.en_US", "en");
    language->setCurrentIndex(language->findData(settings_.lyricLanguage));
    form->addRow(label("ui.audio_import.lyric_language"), language);
    auto *modelRow = new QHBoxLayout;
    auto *model = new QLineEdit(dialog);
    model->setObjectName("audioWhisperModel");
    bindText(model, "ui.audio_import.default_model", "placeholderText");
    model->setText(settings_.whisperModel);
    modelRow->addWidget(model);
    auto *chooseModel = button("ui.audio_import.choose_model", modelRow);
    connect(chooseModel, &QPushButton::clicked, dialog,
            [dialog, model]
            {
                const auto path = QFileDialog::getOpenFileName(dialog, trText("ui.audio_import.choose_model"), {},
                                                               "Whisper (*.bin)");
                if (!path.isEmpty())
                    model->setText(path);
            });
    form->addRow(label("ui.audio_import.model"), modelRow);
    form->addRow(separate);
    form->addRow(recognize);
    form->addRow(wholeSong);
    form->addRow(enhance);
    for (auto *widget :
         std::initializer_list<QWidget *>{language, model, chooseModel, separate, recognize, enhance})
        widget->hide();
    if (auto *caption = form->labelForField(language))
        caption->hide();
    if (auto *caption = form->labelForField(modelRow))
        caption->hide();
    layout->addLayout(form);
    auto *lyrics = new QPlainTextEdit(dialog);
    lyrics->setObjectName("audioInputLyrics");
    bindText(lyrics, "ui.audio_import.lyrics_placeholder", "placeholderText");
    layout->addWidget(lyrics);
    auto *buttons = new QHBoxLayout;
    auto *listen = button("ui.audio_import.listen_selection", buttons);
    listen->setObjectName("audioListenSelection");
    auto *loadLyrics = button("ui.audio_import.load_lyrics", buttons);
    auto *analyze = button("ui.audio_import.analyze", buttons);
    analyze->setObjectName("audioAnalyze");
    layout->addLayout(buttons);
    connect(loadLyrics, &QPushButton::clicked, dialog,
            [dialog, lyrics]
            {
                const QString file = QFileDialog::getOpenFileName(dialog, trText("ui.audio_import.load_lyrics"),
                                                                  {}, "Lyrics (*.txt *.lrc)");
                if (file.isEmpty())
                    return;
                QFile input(file);
                if (input.open(QIODevice::ReadOnly) && input.size() <= 1024 * 1024)
                    lyrics->setPlainText(QString::fromUtf8(input.readAll()));
            });
    connect(listen, &QPushButton::clicked, dialog,
            [this, path, start, audition]
            {
                if (!audition->play(path, start->value()))
                    setStatusMessage(audition->errorString());
                else
                    setStatus("ui.vocal_separation.import_audition");
            });
    connect(analyze, &QPushButton::clicked, dialog,
            [this, dialog, path, start, end, bpm, lyrics, recognize, separate, enhance, wholeSong, language, model]
            {
                AudioTranscriptionOptions options;
                options.startSeconds = start->value();
                options.endSeconds = end->value();
                options.bpm = bpm->value();
                options.lyricsText = lyrics->toPlainText();
                options.recognizeLyrics = recognize->isChecked();
                options.separateVocals = separate->isChecked();
                options.enhanceVoice = options.separateVocals && enhance->isChecked();
                options.wholeSong = wholeSong->isChecked();
                options.language = language->currentData().toString();
                options.whisperModel = model->text().trimmed();
                if (!options.wholeSong && (options.endSeconds <= options.startSeconds ||
                                           options.endSeconds - options.startSeconds > 120))
                {
                    setStatus("messages.audio_import.interval");
                    return;
                }
                const QString capabilityError = audioImportCapabilityError(options);
                if (!capabilityError.isEmpty())
                {
                    setStatusMessage(capabilityError);
                    return;
                }
                audioCandidate_.reset();
                QSettings().setValue("audio/whisperModel", options.whisperModel);
                if (audioTask_.start(path, options))
                    dialog->close();
                else
                    setStatusMessage(audioTask_.errorString());
            });
    const auto importDialog = audioImportDialog_;
    connect(dialog, &QDialog::finished, this,
            [this, importDialog]
            {
                if (audioImportDialog_ == importDialog)
                    audioImportDialog_.clear();
            });
    dialog->show();
}

void MainWindow::refreshAudioTaskUi()
{
    if (!audioTaskStatus_)
        return;
    using State = AudioTranscriptionTask::State;
    const auto state = audioTask_.state();
    if (auto *controls = findChild<QWidget *>("audioTaskControls"))
        controls->setVisible(state != State::Idle);
    audioPreviewButton_->setVisible(state == State::Ready);
    audioDiscardButton_->setVisible(state == State::Ready || state == State::Failed || state == State::Cancelled);
    audioCancelButton_->setVisible(audioTask_.isRunning());
    audioCancelButton_->setEnabled(state == State::Running);
    const char *key = state == State::Running      ? "ui.audio_import.running"
                      : state == State::Cancelling ? "ui.audio_import.cancelling"
                      : state == State::Ready      ? "ui.audio_import.ready"
                      : state == State::Cancelled  ? "ui.audio_import.cancelled"
                                                   : "ui.audio_import.idle";
    audioTaskStatus_->setText(state == State::Failed ? localizeMessage(audioTask_.errorString()) : trText(key));
}

void MainWindow::previewAudioResult()
{
    const auto *result = audioTask_.result();
    if (!result || busy_ || audioLoading_)
        return;
    try
    {
        if (!audioCandidate_)
            audioCandidate_ = transcriptionProject(*result, audioTask_.options());
        if (audioPreview_)
        {
            audioPreview_->show();
            audioPreview_->raise();
            return;
        }
        auto *preview =
            new RecognitionPreviewDialog(*audioCandidate_, QFileInfo(result->sourcePath).fileName(), this);
        preview->setAttribute(Qt::WA_DeleteOnClose);
        preview->setObjectName("audioRecognitionPreview");
        audioPreview_ = preview;
        preview->applyRequested = [this] { applyAudioResult(); };
        auto *audition = new PreviewAudioSession(
            *this, [this] { return playIntent_; }, [this](bool playing) { playIntent_ = playing; },
            [this](const QString &error) { setStatusMessage(error); }, preview);
        audition->setObjectName("audioStemPreviewAudition");
        auto *stemRow = new QHBoxLayout;
        auto *vocals = button("ui.vocal_separation.listen_vocals", stemRow);
        vocals->setObjectName("previewSeparatedVocals");
        auto *instrumental = button("ui.vocal_separation.listen_instrumental", stemRow);
        instrumental->setObjectName("previewSeparatedInstrumental");
        auto *stop = button("ui.vocal_separation.stop_preview", stemRow);
        stop->setObjectName("previewSeparatedStop");
        auto *help = label("ui.vocal_separation.preview_help");
        help->setWordWrap(true);
        stemRow->addWidget(help, 1);
        if (auto *layout = qobject_cast<QVBoxLayout *>(preview->layout()))
            layout->insertLayout(3, stemRow);
        const bool separated = result->vocalsSeparated;
        const QString vocalsPath = result->vocalsPath, instrumentalPath = result->instrumentalPath;
        const double startSeconds = result->selectedStartSeconds;
        vocals->setEnabled(separated && !vocalsPath.isEmpty());
        instrumental->setEnabled(separated && !instrumentalPath.isEmpty());
        auto listen = [this, audition, startSeconds](const QString &path)
        {
            if (!audition->play(path, startSeconds))
                setStatusMessage(audition->errorString());
            else
                setStatus("ui.vocal_separation.preview_audition");
        };
        connect(vocals, &QPushButton::clicked, preview, [listen, vocalsPath] { listen(vocalsPath); });
        connect(instrumental, &QPushButton::clicked, preview,
                [listen, instrumentalPath] { listen(instrumentalPath); });
        connect(stop, &QPushButton::clicked, preview, [audition] { audition->finish(); });
        const auto candidatePreview = audioPreview_;
        connect(preview, &QDialog::finished, this,
                [this, candidatePreview]
                {
                    if (audioPreview_ == candidatePreview)
                        audioPreview_.clear();
                });
        preview->show();
    }
    catch (const std::exception &error)
    {
        setStatusMessage(QString::fromUtf8(error.what()));
    }
}

void MainWindow::applyAudioResult()
{
    if (!audioCandidate_ || !audioTask_.result() || busy_ || audioLoading_)
        return;
    const Project candidate = *audioCandidate_;
    QMessageBox confirm(
        QMessageBox::Question, trText("ui.audio_import.replace_title"),
        trText("ui.audio_import.replace_message").arg(QString::fromStdString(project_.score.title)),
        QMessageBox::Yes | QMessageBox::No, audioPreview_);
    confirm.setObjectName("audioReplaceConfirmation");
    confirm.setDefaultButton(QMessageBox::No);
    if (confirm.exec() != QMessageBox::Yes || !confirmDiscard())
        return;
    if (!audioTask_.result())
        return;
    try
    {
        setProject(candidate, true);
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
        return;
    }
    if (audioPreview_)
        if (auto *audition = dynamic_cast<PreviewAudioSession *>(
                audioPreview_->findChild<QObject *>("audioStemPreviewAudition")))
            audition->abandon();
    discardAudioResult();
    setStatus("ui.audio_import.applied");
}

void MainWindow::discardAudioResult()
{
    if (audioTask_.isRunning())
        return;
    if (audioPreview_)
        audioPreview_->close();
    audioCandidate_.reset();
    audioTask_.discard();
}

bool MainWindow::originalAudioMode() const
{
    return playbackSource_ && playbackSource_->currentData().toInt() > 0 && originalAudio_.isOpen();
}

bool MainWindow::originalMappingCurrent() const
{
    return playbackSource_ &&
           mappingMatchesInput(project_, originalAudio_, playbackSource_->currentData().toInt());
}

void MainWindow::changePlaybackSource()
{
    if (!loading_)
        markModified();
    const int requested = playbackSource_->currentData().toInt();
    const int previous = previousOriginalSource_;
    const auto tick = previous > 0 && mappingMatchesInput(project_, originalAudio_, previous)
                          ? sourceTickAtSeconds(*project_.audioSource, originalAudio_.positionSeconds())
                          : player_.positionTicks();
    auto restoreSelection = [this, previous]
    {
        const QSignalBlocker blocker(playbackSource_);
        playbackSource_->setCurrentIndex(std::max(0, playbackSource_->findData(previous)));
        previousOriginalSource_ = previous;
    };
    if (requested > 0)
    {
        const QString path = project_.audioSource ? inputPath(*project_.audioSource, requested) : QString();
        if (path.isEmpty())
        {
            setStatus(requested == 1 ? "ui.audio_import.no_original" : "ui.vocal_separation.no_stems");
            restoreSelection();
            return;
        }
        if (!QFileInfo(path).isFile())
        {
            setStatus("messages.vocal_separation.missing_playback_file", {path});
            restoreSelection();
            return;
        }
        const QString previousPath = originalAudio_.sourcePath();
        const double previousSpeed = originalAudio_.speed();
        const bool wasOriginalPlaying = originalAudio_.isPlaying(), wasMelodyPlaying = player_.isPlaying();
        const bool previousIntent = playIntent_;
        player_.pause();
        if (originalAudio_.isOpen())
            originalAudio_.pause();
        const double previousSeconds = originalAudio_.positionSeconds();
        const auto previousTick = player_.positionTicks();
        auto restorePlayback = [&](QString error)
        {
            bool restored = true;
            if (previousPath.isEmpty())
                originalAudio_.close();
            else
            {
                restored = originalAudio_.open(previousPath) && originalAudio_.setSpeed(previousSpeed) &&
                           originalAudio_.seek(previousSeconds) && (!wasOriginalPlaying || originalAudio_.play());
                if (!restored)
                {
                    error += '\n' + originalAudio_.errorString();
                    originalAudio_.close();
                }
            }
            if (wasMelodyPlaying && !player_.play())
            {
                restored = false;
                error += '\n' + player_.errorString();
            }
            playIntent_ = restored && previousIntent;
            restoreSelection();
            setStatusMessage(error);
        };
        bool opened = originalAudio_.open(path);
        if (opened)
            if (auto *speed = findChild<QDoubleSpinBox *>("originalSpeed"))
                opened = originalAudio_.setSpeed(speed->value());
        if (!opened)
        {
            restorePlayback(originalAudio_.errorString());
            return;
        }
        originalAudio_.pause();
        playIntent_ = false;
        previousOriginalSource_ = requested;
        if (originalMappingCurrent())
        {
            const auto mappedTick =
                previous > 0 ? sourceTickAtSeconds(*project_.audioSource, previousSeconds) : previousTick;
            if (!originalAudio_.seek(sourceSecondsAtTick(*project_.audioSource, mappedTick)))
            {
                restorePlayback(originalAudio_.errorString());
                return;
            }
        }
        setStatus(requested == 1 ? "ui.audio_import.original_mode_help" : "ui.vocal_separation.stem_mode_help");
    }
    else
    {
        player_.pause();
        if (originalAudio_.isOpen())
            originalAudio_.pause();
        playIntent_ = false;
        previousOriginalSource_ = 0;
        player_.seek(tick);
    }
    updatePlayback();
}

std::int64_t MainWindow::practicePositionTick() const
{
    return originalAudioMode() && originalMappingCurrent()
               ? sourceTickAtSeconds(*project_.audioSource, originalAudio_.positionSeconds())
               : player_.positionTicks();
}

void MainWindow::seekPracticeTick(std::int64_t tick)
{
    if (originalAudioMode())
    {
        if (originalMappingCurrent())
            originalAudio_.seek(sourceSecondsAtTick(*project_.audioSource, tick));
        else
            setStatus("ui.audio_import.mapping_stale");
    }
    else
        player_.seek(tick);
}

void MainWindow::updateOriginalPlayback()
{
    const double seconds = originalAudio_.positionSeconds();
    const bool mapped = originalMappingCurrent();
    auto tick = mapped ? sourceTickAtSeconds(*project_.audioSource, seconds) : 0;
    if (mapped && playIntent_ && loop_->isChecked() && loopEnd_ > loopStart_ && tick >= loopEnd_)
    {
        originalAudio_.seek(sourceSecondsAtTick(*project_.audioSource, loopStart_));
        if (!originalAudio_.isPlaying())
            originalAudio_.play();
        tick = loopStart_;
    }
    playbackActive_ = originalAudio_.isPlaying();
    if (!playbackActive_ && seconds >= originalAudio_.durationSeconds() - 0.03)
        playIntent_ = false;
    play_->setText(trText(playbackActive_ ? "ui.transport.pause" : "ui.transport.play"));
    time_->setText(QString("%1 / %2 s").arg(seconds, 0, 'f', 1).arg(originalAudio_.durationSeconds(), 0, 'f', 1));
    if (!position_->isSliderDown())
        position_->setValue(int(tick));
    const bool inSelection = mapped && seconds >= project_.audioSource->selectedStartSeconds &&
                             seconds < project_.audioSource->selectedEndSeconds;
    const auto event = inSelection ? timeline_.eventIndexAtTick(tick) : std::nullopt;
    if (event)
    {
        const auto &note = timeline_.events[*event];
        playingNote_ = int(note.sourceNoteIndex);
        playingVerse_ = note.verseIndex;
        playingProgram_ = note.program;
        playingLyric_ = QString::fromStdString(note.lyric);
        refreshPlaybackText();
        view_->setCurrent(playingNote_, playbackActive_);
    }
    else
    {
        lyric_->setText(trText(mapped ? "ui.audio_import.outside_selection" : "ui.audio_import.mapping_stale"));
        view_->setCurrent(-1, false);
    }
}

} // namespace singlilt
