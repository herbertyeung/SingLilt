// Score workspace, playback controls, and import coordination.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include "audio/OriginalAudioPlayer.h"
#include "audio/PlaybackEngine.h"
#include "domain/AccompanimentTimeline.h"
#include "recognition/AudioTranscriptionTask.h"
#include "recognition/CloudRecognitionTask.h"
#include "recognition/CloudRecognizer.h"
#include "recognition/LocalStaffRecognitionTask.h"
#include "settings/AppSettings.h"
#include "storage/ProjectStore.h"
#include <QFutureWatcher>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QUndoStack>
class QBoxLayout;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QPlainTextEdit;
class QCloseEvent;
class QVBoxLayout;
class QApplication;
class QCommandLineParser;
class QAction;
class QMenu;
class QTimer;
class QTabWidget;
class QStackedWidget;
namespace singlilt
{
class PreviewAudioSession;
class ScoreView;
class RecognitionPreviewDialog;
class LanguageManager;
class ThemeManager;
class AccompanimentPanel;
class ClassroomDialog;
class StaffNoteEditor;
class MainWindow : public QMainWindow
{
  public:
    MainWindow(LanguageManager &languageManager, ThemeManager &themes, QWidget *parent = nullptr);
    ~MainWindow() override;
    void openFile(const QString &path);
    void importMusicXml(const QString &path, int trackIndex = -1);
    void importStaffImage(const QString &path);
    void importStaffImages(const QStringList &paths);
    bool startLocalStaffRecognitionPages(std::vector<StaffPageInput> pages, QString label,
                                         LocalStaffRecognitionOptions options = {});
    void setStaffPage(int pageIndex, bool fit = true);
    int staffPageIndex() const
    {
        return staffPageIndex_;
    }
    bool startLocalStaffRecognition(QImage image, QString path, LocalStaffRecognitionOptions options = {});
    LocalStaffRecognitionTask &localStaffRecognitionTask()
    {
        return localStaffTask_;
    }
    void setNotationStyle(NotationStyle style);
    void setProject(Project project, bool modified = false);
    void openStaffCorrection();
    void confirmStaffTempo();
    void applyStaffCorrection(Project corrected);
    PlaybackEngine &player()
    {
        return player_;
    }
    const Project &project() const
    {
        return project_;
    }
    const Timeline &timeline() const
    {
        return timeline_;
    }
    ProjectPracticeSettings currentProjectPractice() const;
    bool hasUnsavedChanges() const
    {
        return dirty_;
    }
    bool isRecognizing() const
    {
        return busy_ || cloudTask_.isRunning() || localStaffTask_.isRunning() || audioTask_.isRunning();
    }
    CloudRecognitionTask &cloudRecognitionTask()
    {
        return cloudTask_;
    }
    bool isAudioLoading() const
    {
        return audioLoading_;
    }
    AudioTranscriptionTask &audioTranscriptionTask()
    {
        return audioTask_;
    }
    OriginalAudioPlayer &originalAudioPlayer()
    {
        return originalAudio_;
    }
    void openAudioImport(const QString &path);
    void openClassroom();
    void openOptions(int page = 0);
    bool applyOptions(const AppSettings &settings, const OptionsContext &context);
    OptionsContext optionsContext() const;
    const AppSettings &appSettings() const
    {
        return settings_;
    }

  protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

  private:
    friend void runHistoryRecoveryCheck(MainWindow &window, LanguageManager &languages,
                                        const QCommandLineParser &args, QApplication &app);
    void retranslateUi();
    void refreshPlaybackText();
    void refreshIssues();
    void bindText(QObject *object, const char *key, const char *property = "text");
    QLabel *label(const char *key);
    QPushButton *button(const char *key, QBoxLayout *layout);
    QCheckBox *checkBox(const char *key);
    void addTranslatedItem(QComboBox *combo, const char *key, const QVariant &value,
                           const QStringList &arguments = {});
    void setStatus(const char *key, const QStringList &arguments = {});
    void setStatusMessage(const QString &message);
    QString chooseFile(bool saving, bool projectOnly = false);
    void showMessage(const QString &text, const char *titleKey, bool warning);
    void createUi();
    void refreshNotationControls();
    void selectStaffNote(int index);
    void beginStaffAnchorEdit(int index);
    void moveStaffAnchor(int index, const SourceRect &anchor);
    void refreshStaffAnchorEditing();
    void refreshStaffAnchorView();
    bool canEditOriginalStaff(bool allowDraft = false) const;
    void addStaffNoteAt(QPointF position, std::optional<int> sameBeatReference = {});
    void deleteStaffNote(int index);
    void applyStaffNoteEdit(Project corrected, int selectedIndex, const char *undoKey);
    void moveStaffNote(int index, const SourceRect &anchor, bool pitchEditing, bool timingEditing);
    bool applyStaffInspectorNote();
    void cancelStaffInspectorNote();
    void refreshStaffNoteInspector();
    void previewStaffInspectorNote(const StaffPerformanceNote &before, const StaffPerformanceNote &note);
    void auditionStaffNote(const StaffPerformanceNote &note);
    void scheduleStaffEditRecovery();
    bool saveStaffEditRecoverySnapshot();
    void recoverStaffEdits();
    void createHeader(QVBoxLayout *layout);
    void createMenus();
    void startCloudRecognition();
    void exportWaveFile();
    void createScoreOptions(QVBoxLayout *layout);
    QWidget *createInspector();
    void createScorePane(QVBoxLayout *layout);
    void createTransport(QVBoxLayout *layout);
    void createPracticeControls(QVBoxLayout *layout);
    void generatePracticeAccompaniment();
    bool auditionAccompaniment(const AccompanimentArrangement &arrangement);
    bool confirmAccompaniment(const AccompanimentArrangement &arrangement);
    void cancelAccompanimentPreview();
    void updatePracticeMix();
    void refreshPracticeControls();
    void changeAccompanimentPattern();
    void chooseGmSoundFont();
    void createAudioControls(QVBoxLayout *layout);
    void populateAudioImportDialog(const QString &path, QDialog *dialog, PreviewAudioSession *audition);
    void refreshAudioTaskUi();
    void previewAudioResult();
    void applyAudioResult();
    void discardAudioResult();
    void changePlaybackSource();
    bool originalAudioMode() const;
    bool originalMappingCurrent() const;
    void updateOriginalPlayback();
    void seekPracticeTick(std::int64_t tick);
    std::int64_t practicePositionTick() const;
    void editLyrics();
    AccompanimentPlan activeAccompanimentPlan() const;
    void recognize(QImage image, QString path);
    void openPracticeExample(bool imageOnly);
    void createCloudTaskBanner(QVBoxLayout *layout);
    void createLocalStaffTaskBanner(QVBoxLayout *layout);
    void refreshStaffPageControls();
    void refreshStaffSourceView();
    ScoreView *displayedScoreView() const;
    void refreshLocalStaffTaskUi();
    void previewLocalStaffResult();
    void applyLocalStaffResult();
    void discardLocalStaffResult();
    void refreshCloudTaskUi();
    void previewCloudResult();
    void applyCloudResult();
    void discardCloudResult();
    void selectNote(int index, bool seek = true);
    void updateNote();
    void rebuild(bool preservePosition);
    void updatePlayback();
    void togglePlayback();
    void seekVerse(int verse);
    void updateVersePrograms();
    void save(bool saveAs = false);
    void newProject();
    void restoreProjectPractice();
    bool confirmDiscard();
    void configureCloud();
    void editRepeats();
    void showError(const QString &text);
    struct MaterialState
    {
        std::vector<Note> notes;
        std::vector<RepeatSection> repeats;
        QStringList warnings;
        std::vector<AudioLyricTiming> lyricTimings;
        int selected = -1;
        std::int64_t loopStart = 0, loopEnd = 0;
        std::uint64_t loopRevision = 0;
        std::optional<Project> staffProject;
        int selectedStaffNote = -1;
    };
    MaterialState materialState() const;
    void commitMaterialEdit(const MaterialState &before, const char *key, bool anchorOnly = false);
    void restoreMaterialState(const MaterialState &state);
    void restoreStaffAnchorState(const MaterialState &state);
    void undoMaterialEdit();
    void redoMaterialEdit();
    bool hasNoteDraft() const;
    bool resolveNoteDraft();
    void removeNote();
    void insertNote(QPointF position);
    void setCorrectionMode(bool correction);
    void refreshReviewStatus();
    void nextUncertainNote();
    void markModified();
    void refreshProjectIdentity();
    void refreshSourceControls();
    void rememberProject(const QString &path);
    void refreshRecentProjects();
    LanguageManager &languageManager_;
    ThemeManager &themes_;
    Project project_;
    Timeline timeline_;
    PlaybackEngine player_;
    OriginalAudioPlayer originalAudio_;
    AudioTranscriptionTask audioTask_;
    std::optional<Project> audioCandidate_;
    QPointer<RecognitionPreviewDialog> audioPreview_;
    QPointer<QDialog> audioImportDialog_;
    VisionConfig vision_;
    AppSettings settings_;
    CloudRecognitionTask cloudTask_;
    LocalStaffRecognitionTask localStaffTask_;
    QPointer<RecognitionPreviewDialog> localStaffPreview_;
    QWidget *localStaffBanner_ = nullptr;
    QLabel *localStaffStatus_ = nullptr;
    QPushButton *localStaffPreviewButton_ = nullptr;
    QPushButton *localStaffCancelButton_ = nullptr;
    QPushButton *localStaffDiscardButton_ = nullptr;
    QPointer<RecognitionPreviewDialog> cloudPreview_;
    QPointer<AccompanimentPanel> accompanimentPanel_;
    QPointer<ClassroomDialog> classroom_;
    std::optional<AccompanimentArrangement> auditionArrangement_;
    std::optional<PracticeMix> preAuditionMix_;
    std::optional<int> preAuditionPlaybackSource_;
    PracticeMix auditionMix_;
    QFutureWatcher<RecognitionResult> watcher_;
    QFutureWatcher<bool> audioWatcher_;
    QImage pendingImage_;
    QImage localStaffOriginalImage_;
    QString projectPath_;
    QTimer *staffRecoveryTimer_ = nullptr;
    QString staffRecoverySession_;
    QString staffRecoveryPath_;
    bool staffRecoveryPending_ = false;
    QString debugText_;
    int selected_ = -1;
    int selectedStaffNote_ = -1;
    int playingNote_ = -1;
    int playingVerse_ = -1;
    bool loading_ = false, dirty_ = false, busy_ = false;
    bool nonMaterialDirty_ = false;
    bool loadingNote_ = false;
    bool correctionMode_ = false;
    QUndoStack materialUndo_;
    QWidget *inspectorScroll_ = nullptr;
    QPushButton *practiceModeButton_ = nullptr;
    QPushButton *correctionModeButton_ = nullptr;
    QComboBox *notationStyle_ = nullptr;
    QComboBox *staffPageSelector_ = nullptr;
    QWidget *staffPageToolbar_ = nullptr;
    QCheckBox *staffAutoPage_ = nullptr;
    QPushButton *staffPreviousPage_ = nullptr, *staffNextPage_ = nullptr;
    int staffPageIndex_ = 0;
    QDoubleSpinBox *practiceSpeed_ = nullptr;
    QLabel *projectIdentity_ = nullptr, *reviewStatus_ = nullptr;
    QMenu *recentProjects_ = nullptr;
    QAction *undoAction_ = nullptr, *redoAction_ = nullptr;
    bool playIntent_ = false;
    bool audioLoading_ = false;
    bool notePreviewLoading_ = false;
    QString lastAudioError_;
    QString statusSource_;
    QStringList statusArguments_;
    bool statusIsKey_ = true;
    bool playbackActive_ = false;
    int playingProgram_ = 0;
    QString playingLyric_;
    int64_t loopStart_ = 0, loopEnd_ = 0;
    std::uint64_t loopEditRevision_ = 0;
    ScoreView *view_ = nullptr;
    ScoreView *staffSourceView_ = nullptr;
    QTabWidget *staffScoreTabs_ = nullptr;
    QStackedWidget *noteInspectorStack_ = nullptr;
    StaffNoteEditor *staffNoteEditor_ = nullptr;
    int staffInspectorNote_ = -1;
    QLabel *title_ = nullptr, *subtitle_ = nullptr, *time_ = nullptr, *lyric_ = nullptr, *noteTitle_ = nullptr,
           *status_ = nullptr, *verseStatus_ = nullptr;
    QPushButton *play_ = nullptr, *import_ = nullptr, *cloud_ = nullptr;
    QWidget *cloudTaskBanner_ = nullptr;
    QLabel *cloudTaskStatus_ = nullptr;
    QPushButton *cloudTaskPreview_ = nullptr, *cloudTaskCancel_ = nullptr, *cloudTaskDiscard_ = nullptr;
    QSlider *position_ = nullptr, *volume_ = nullptr;
    QDoubleSpinBox *tempo_ = nullptr, *duration_ = nullptr;
    QSpinBox *octave_ = nullptr, *accidental_ = nullptr, *transpose_ = nullptr, *meterTop_ = nullptr,
             *velocity_ = nullptr;
    QComboBox *key_ = nullptr, *meterBottom_ = nullptr, *degree_ = nullptr, *noteKey_ = nullptr,
              *verseView_ = nullptr, *programA_ = nullptr, *programB_ = nullptr, *audioSource_ = nullptr,
              *languageSelector_ = nullptr;
    QLineEdit *lyricEdit_ = nullptr, *lyricBEdit_ = nullptr;
    QCheckBox *metronome_ = nullptr, *tie_ = nullptr, *loop_ = nullptr, *sharedLyric_ = nullptr,
              *accentBeats_ = nullptr;
    QPlainTextEdit *issues_ = nullptr;
    QCheckBox *melodyEnabled_ = nullptr, *accompanimentEnabled_ = nullptr;
    QSlider *accompanimentVolume_ = nullptr;
    QComboBox *accompanimentPattern_ = nullptr;
    QLabel *accompanimentState_ = nullptr;
    QComboBox *playbackSource_ = nullptr;
    QLabel *audioTaskStatus_ = nullptr;
    int previousOriginalSource_ = 0;
    QPushButton *audioPreviewButton_ = nullptr, *audioCancelButton_ = nullptr, *audioDiscardButton_ = nullptr;
};
} // namespace singlilt
