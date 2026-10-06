// Lesson practice, recording, and singing-feedback window.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "audio/OriginalAudioPlayer.h"
#include "audio/PlaybackEngine.h"
#include "domain/EarTraining.h"
#include "domain/SingingLesson.h"
#include "practice/PracticeSession.h"
#include "settings/AppSettings.h"
#include "storage/PracticeHistory.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <functional>
#include <optional>
#include <random>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QTextEdit;
class QProgressBar;
class QCheckBox;
class QTimer;
class QApplication;
class QCommandLineParser;

namespace singlilt
{
class LanguageManager;
class ScoreView;
class PitchCurve;
class MainWindow;
class ClassroomDialog final : public QDialog
{
  public:
    ClassroomDialog(LanguageManager &languages, AudioBackend backend, QString lessonDirectory, QString historyPath,
                    QWidget *parent = nullptr);
    ~ClassroomDialog() override;
    void reloadLessons(const QString &directory);
    const std::vector<SingingLesson> &lessons() const
    {
        return lessons_;
    }
    const EarQuestion &question() const
    {
        return question_;
    }
    PlaybackEngine &player()
    {
        return player_;
    }
    std::function<void(Score)> openLessonScore;
    void showEarTraining();
    void openOptions();
    void reject() override;
    void done(int result) override;

  protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    friend void runHistoryRecoveryCheck(MainWindow &window, LanguageManager &languages,
                                        const QCommandLineParser &args, QApplication &app);
    void createUi();
    void retranslate();
    void selectLesson(int index);
    void updateLessonText();
    void updatePhrase(int sourceNoteIndex);
    void auditionNote(const Score &score, int index, int transpose);
    Score currentLessonScore() const;
    Score questionScore(bool includeReference) const;
    void refreshDevices();
    void playExample();
    void stopActivity();
    void startRecording();
    void tick();
    void finishRecording();
    void displayResult(const SingingResult &result);
    void newQuestion(bool weakOnly = false);
    void answerQuestion();
    void renderOptions();
    void revealQuestion();
    bool appendHistory(QJsonObject attempt);
    bool retryHistory();
    void refreshHistorySaveUi();
    bool confirmHistoryDiscard();
    void refreshHistory();
    void setBusy(bool busy);
    void showError(const QString &message);
    bool earMode() const;
    QString selectedDevice() const;
    LanguageManager &languages_;
    QString lessonDirectory_;
    std::vector<SingingLesson> lessons_;
    PlaybackEngine player_;
    OriginalAudioPlayer replay_;
    QFutureWatcher<bool> audioLoader_;
    PracticeSession session_;
    PracticeHistory history_;
    QJsonArray pendingHistory_;
    QString historyError_;
    QTemporaryDir recordingDirectory_;
    std::mt19937 random_{std::random_device{}()};
    EarQuestion question_;
    Score exerciseScore_;
    Timeline exerciseTimeline_;
    std::vector<ExpectedTone> targets_;
    std::vector<std::size_t> sourceMap_;
    std::optional<SingingResult> lastResult_;
    int lessonIndex_ = -1;
    int phraseFirst_ = 0, phraseEnd_ = 0;
    int heardCount_ = 0;
    bool answered_ = false, heard_ = false, demonstrating_ = false;
    bool recording_ = false, calibrating_ = false, audioReady_ = false, historyWritable_ = true;
    bool startedGuide_ = false;
    bool previewStarting_ = false;
    AppSettings preferences_;
    QPushButton *settingsButton_ = nullptr;
    double recordStart_ = 0.0;
    QString lastError_;
    QTimer *timer_ = nullptr;
    QComboBox *lessonSelector_ = nullptr, *devices_ = nullptr, *exerciseType_ = nullptr, *difficulty_ = nullptr,
              *range_ = nullptr, *guide_ = nullptr;
    QSpinBox *transpose_ = nullptr, *latency_ = nullptr;
    QDoubleSpinBox *speed_ = nullptr, *tolerance_ = nullptr;
    QLabel *status_ = nullptr, *live_ = nullptr, *earPrompt_ = nullptr, *earFeedback_ = nullptr,
           *summary_ = nullptr, *folder_ = nullptr, *historySaveStatus_ = nullptr;
    QTextEdit *instructions_ = nullptr;
    ScoreView *scoreView_ = nullptr, *answerScore_ = nullptr;
    PitchCurve *curve_ = nullptr;
    QProgressBar *level_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QTableWidget *options_ = nullptr, *results_ = nullptr, *historyTable_ = nullptr;
    QPushButton *listen_ = nullptr, *record_ = nullptr, *stop_ = nullptr, *calibrate_ = nullptr,
                *replayButton_ = nullptr, *exportButton_ = nullptr, *submit_ = nullptr, *next_ = nullptr,
                *weak_ = nullptr, *chooseFolder_ = nullptr, *reload_ = nullptr, *openScore_ = nullptr,
                *refreshMic_ = nullptr, *retryHistory_ = nullptr;
    QCheckBox *loop_ = nullptr;
};
} // namespace singlilt
