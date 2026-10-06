// Classroom widgets, score display, and feedback layout.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ClassroomDialog.h"
#include "MenuIcons.h"
#include "PitchCurve.h"
#include "ScoreView.h"
#include "i18n/LanguageManager.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace singlilt
{
namespace
{
QPushButton *action(const char *key, const char *name, QBoxLayout *layout)
{
    auto *button = new QPushButton(trText(key));
    button->setObjectName(name);
    button->setProperty("_ui_text", QByteArray(key));
    layout->addWidget(button);
    return button;
}
QLabel *caption(const char *key, QBoxLayout *layout)
{
    auto *label = new QLabel(trText(key));
    label->setProperty("_ui_text", QByteArray(key));
    layout->addWidget(label);
    return label;
}
void configureTable(QTableWidget *table, const char *name)
{
    table->setObjectName(name);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->hide();
}
} // namespace

void ClassroomDialog::createUi()
{
    setObjectName("singingClassroom");
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1240, 910);
    setMinimumSize(1020, 760);
    auto *root = new QVBoxLayout(this);
    auto *catalog = new QHBoxLayout;
    caption("ui.classroom.lesson", catalog);
    lessonSelector_ = new QComboBox;
    lessonSelector_->setObjectName("classroomLesson");
    catalog->addWidget(lessonSelector_, 1);
    chooseFolder_ = action("ui.classroom.open_folder", "classroomOpenFolder", catalog);
    reload_ = action("ui.classroom.reload", "classroomReload", catalog);
    openScore_ = action("ui.classroom.open_score", "classroomOpenScore", catalog);
    settingsButton_ = action("ui.options.title", "classroomOptions", catalog);
    connect(settingsButton_, &QPushButton::clicked, this, [this] { openOptions(); });
    root->addLayout(catalog);
    folder_ = new QLabel;
    folder_->setTextFormat(Qt::PlainText);
    folder_->setWordWrap(true);
    root->addWidget(folder_);

    tabs_ = new QTabWidget;
    tabs_->setObjectName("classroomTabs");
    auto *lesson = new QWidget;
    auto *lessonLayout = new QVBoxLayout(lesson);
    instructions_ = new QTextEdit;
    instructions_->setObjectName("classroomInstructions");
    instructions_->setReadOnly(true);
    instructions_->setMaximumHeight(130);
    lessonLayout->addWidget(instructions_);
    scoreView_ = new ScoreView;
    scoreView_->setObjectName("classroomScore");
    scoreView_->setMinimumHeight(130);
    lessonLayout->addWidget(scoreView_, 1);
    auto *phrase = new QHBoxLayout;
    caption("ui.classroom.range", phrase);
    range_ = new QComboBox;
    range_->setObjectName("classroomRange");
    phrase->addWidget(range_);
    loop_ = new QCheckBox;
    loop_->setProperty("_ui_text", QByteArray("ui.classroom.loop"));
    phrase->addWidget(loop_);
    phrase->addStretch();
    lessonLayout->addLayout(phrase);
    tabs_->addTab(lesson, QString());

    auto *ear = new QWidget;
    auto *earLayout = new QVBoxLayout(ear);
    auto *questions = new QHBoxLayout;
    exerciseType_ = new QComboBox;
    exerciseType_->setObjectName("earExercise");
    difficulty_ = new QComboBox;
    difficulty_->setObjectName("earDifficulty");
    questions->addWidget(exerciseType_);
    difficulty_->setParent(ear);
    difficulty_->hide();
    next_ = action("ui.ear.next", "earNext", questions);
    weak_ = action("ui.ear.weak", "earWeak", questions);
    questions->addStretch();
    earLayout->addLayout(questions);
    earPrompt_ = new QLabel;
    earPrompt_->setWordWrap(true);
    earPrompt_->setTextFormat(Qt::PlainText);
    earLayout->addWidget(earPrompt_);
    options_ = new QTableWidget(0, 1);
    configureTable(options_, "earOptions");
    options_->setMaximumHeight(140);
    earLayout->addWidget(options_);
    auto *answer = new QHBoxLayout;
    submit_ = action("ui.ear.submit", "earSubmit", answer);
    earFeedback_ = new QLabel;
    earFeedback_->setTextFormat(Qt::PlainText);
    earFeedback_->setWordWrap(true);
    answer->addWidget(earFeedback_, 1);
    earLayout->addLayout(answer);
    answerScore_ = new ScoreView;
    answerScore_->setObjectName("earAnswerScore");
    answerScore_->setMinimumHeight(100);
    answerScore_->hide();
    earLayout->addWidget(answerScore_, 1);
    tabs_->addTab(ear, QString());

    historyTable_ = new QTableWidget(0, 5);
    configureTable(historyTable_, "practiceHistory");
    tabs_->addTab(historyTable_, QString());
    root->addWidget(tabs_, 1);

    auto *inputSettings = new QWidget(this);
    inputSettings->setObjectName("legacyMicrophoneSettings");
    auto *input = new QHBoxLayout(inputSettings);
    caption("ui.classroom.microphone", input);
    devices_ = new QComboBox;
    devices_->setObjectName("classroomMicrophone");
    input->addWidget(devices_, 1);
    refreshMic_ = action("ui.classroom.refresh_mic", "classroomRefreshMic", input);
    calibrate_ = action("ui.classroom.calibrate", "classroomCalibrate", input);
    level_ = new QProgressBar;
    level_->setObjectName("classroomInputLevel");
    level_->setRange(0, 100);
    level_->setMaximumWidth(110);
    input->addWidget(level_);
    inputSettings->hide();

    auto *performanceSettings = new QWidget(this);
    performanceSettings->setObjectName("legacyClassroomSettings");
    auto *settings = new QHBoxLayout(performanceSettings);
    caption("ui.classroom.transpose", settings);
    transpose_ = new QSpinBox;
    transpose_->setObjectName("classroomTranspose");
    transpose_->setRange(-12, 12);
    settings->addWidget(transpose_);
    caption("ui.classroom.speed", settings);
    speed_ = new QDoubleSpinBox;
    speed_->setObjectName("classroomSpeed");
    speed_->setRange(0.5, 1.5);
    speed_->setSingleStep(0.1);
    speed_->setValue(1.0);
    settings->addWidget(speed_);
    caption("ui.classroom.tolerance", settings);
    tolerance_ = new QDoubleSpinBox;
    tolerance_->setObjectName("classroomTolerance");
    tolerance_->setRange(10.0, 100.0);
    tolerance_->setValue(50.0);
    tolerance_->setDecimals(0);
    settings->addWidget(tolerance_);
    caption("ui.classroom.latency", settings);
    latency_ = new QSpinBox;
    latency_->setObjectName("classroomLatency");
    latency_->setRange(0, 500);
    latency_->setSingleStep(10);
    settings->addWidget(latency_);
    guide_ = new QComboBox;
    guide_->setObjectName("classroomGuide");
    settings->addWidget(guide_, 1);
    performanceSettings->hide();

    auto *transport = new QHBoxLayout;
    listen_ = action("ui.classroom.listen", "classroomListen", transport);
    record_ = action("ui.classroom.record", "classroomRecord", transport);
    stop_ = action("ui.classroom.stop", "classroomStop", transport);
    replayButton_ = action("ui.classroom.replay", "classroomReplay", transport);
    exportButton_ = action("ui.classroom.export", "classroomExport", transport);
    live_ = new QLabel;
    transport->addWidget(live_, 1);
    transport->addWidget(level_);
    root->addLayout(transport);
    curve_ = new PitchCurve;
    root->addWidget(curve_);
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    root->addWidget(summary_);
    results_ = new QTableWidget(0, 5);
    configureTable(results_, "singingResults");
    results_->setMaximumHeight(105);
    root->addWidget(results_);
    status_ = new QLabel;
    status_->setObjectName("classroomStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    root->addWidget(status_);

    auto *historyStatus = new QHBoxLayout;
    historySaveStatus_ = new QLabel;
    historySaveStatus_->setObjectName("historySaveStatus");
    historySaveStatus_->setTextFormat(Qt::PlainText);
    historySaveStatus_->setWordWrap(true);
    historyStatus->addWidget(historySaveStatus_, 1);
    retryHistory_ = action("ui.history.retry", "historyRetry", historyStatus);
    connect(retryHistory_, &QPushButton::clicked, this,
            [this]
            {
                if (retryHistory())
                    status_->setText(trText("ui.history.saved"));
                setBusy(recording_ || calibrating_ || demonstrating_ || previewStarting_);
            });
    root->addLayout(historyStatus);

    connect(lessonSelector_, &QComboBox::currentIndexChanged, this, [this](int index) { selectLesson(index); });
    connect(chooseFolder_, &QPushButton::clicked, this,
            [this]
            {
                const QString directory =
                    QFileDialog::getExistingDirectory(this, trText("ui.classroom.open_folder"), lessonDirectory_);
                if (directory.isEmpty())
                    return;
                try
                {
                    reloadLessons(directory);
                }
                catch (const std::exception &error)
                {
                    showError(QString::fromUtf8(error.what()));
                }
            });
    connect(reload_, &QPushButton::clicked, this,
            [this]
            {
                try
                {
                    reloadLessons(lessonDirectory_);
                }
                catch (const std::exception &error)
                {
                    showError(QString::fromUtf8(error.what()));
                }
            });
    connect(openScore_, &QPushButton::clicked, this,
            [this]
            {
                if (lessonIndex_ >= 0 && openLessonScore)
                    openLessonScore(lessons_[static_cast<std::size_t>(lessonIndex_)].score);
            });
    connect(refreshMic_, &QPushButton::clicked, this, [this] { refreshDevices(); });
    connect(calibrate_, &QPushButton::clicked, this,
            [this]
            {
                if (selectedDevice().isEmpty())
                    return;
                stopActivity();
                calibrating_ = true;
                session_.calibrateNoise(selectedDevice());
                setBusy(true);
                status_->setText(trText("ui.classroom.quiet"));
            });
    connect(listen_, &QPushButton::clicked, this, [this] { playExample(); });
    connect(record_, &QPushButton::clicked, this, [this] { startRecording(); });
    connect(stop_, &QPushButton::clicked, this, [this] { stopActivity(); });
    connect(replayButton_, &QPushButton::clicked, this,
            [this]
            {
                try
                {
                    stopActivity();
                    const QString path = recordingDirectory_.filePath("attempt.wav");
                    session_.exportWave(path);
                    if (!replay_.open(path) || !replay_.play())
                        throw std::runtime_error(replay_.errorString().toStdString());
                    stop_->setEnabled(true);
                    status_->setText(trText("ui.classroom.replaying"));
                }
                catch (const std::exception &error)
                {
                    showError(QString::fromUtf8(error.what()));
                }
            });
    connect(exportButton_, &QPushButton::clicked, this,
            [this]
            {
                const QString path = QFileDialog::getSaveFileName(this, trText("ui.classroom.export"),
                                                                  "practice.wav", "WAV (*.wav)");
                if (path.isEmpty())
                    return;
                try
                {
                    session_.exportWave(path);
                    status_->setText(trText("ui.classroom.exported").arg(path));
                }
                catch (const std::exception &error)
                {
                    showError(QString::fromUtf8(error.what()));
                }
            });
    connect(next_, &QPushButton::clicked, this, [this] { newQuestion(); });
    connect(weak_, &QPushButton::clicked, this, [this] { newQuestion(true); });
    connect(submit_, &QPushButton::clicked, this, [this] { answerQuestion(); });
    connect(exerciseType_, &QComboBox::currentIndexChanged, this, [this] { newQuestion(); });
    connect(difficulty_, &QComboBox::currentIndexChanged, this, [this] { newQuestion(); });
    connect(transpose_, &QSpinBox::valueChanged, this,
            [this]
            {
                heard_ = false;
                if (earMode())
                    newQuestion();
                setBusy(false);
            });
    connect(speed_, &QDoubleSpinBox::valueChanged, this,
            [this]
            {
                heard_ = false;
                setBusy(false);
            });
    connect(range_, &QComboBox::currentIndexChanged, this,
            [this]
            {
                heard_ = false;
                setBusy(false);
            });
    connect(tabs_, &QTabWidget::currentChanged, this,
            [this]
            {
                stopActivity();
                heard_ = false;
                if (earMode())
                    newQuestion();
                setBusy(false);
            });
    scoreView_->noteClicked = [this](int index)
    {
        if (!recording_ && !demonstrating_ && !calibrating_ && !previewStarting_)
        {
            updatePhrase(index);
            auditionNote(lessons_[static_cast<std::size_t>(lessonIndex_)].score, index, transpose_->value());
        }
    };
    answerScore_->noteClicked = [this](int index)
    {
        if (answered_ && !recording_ && !demonstrating_)
            auditionNote(questionScore(true), index, 0);
    };
    connect(results_, &QTableWidget::cellClicked, this,
            [this](int row, int)
            {
                if (earMode() || recording_ || demonstrating_ || calibrating_ || previewStarting_ || row < 0 ||
                    !results_->item(row, 0))
                    return;
                updatePhrase(results_->item(row, 0)->data(Qt::UserRole).toInt());
                range_->setCurrentIndex(1);
                status_->setText(trText("ui.classroom.retry_phrase"));
            });
    retranslate();
    auto *menus = new QMenuBar(this);
    root->setMenuBar(menus);
    const auto menu = [menus](const char *key)
    {
        auto *result = menus->addMenu(trText(key));
        result->setProperty("_ui_title", QByteArray(key));
        return result;
    };
    const auto linked = [this](QMenu *menu, QPushButton *button, MenuIcon icon)
    {
        auto *action = menu->addAction(menuIcon(icon), button->text());
        action->setObjectName(button->objectName() + "MenuAction");
        action->setIconVisibleInMenu(true);
        action->setProperty("_ui_text", button->property("_ui_text"));
        connect(action, &QAction::triggered, this, [button] { button->click(); });
    };
    auto *files = menu("ui.menu.file");
    linked(files, chooseFolder_, MenuIcon::Folder);
    linked(files, reload_, MenuIcon::Refresh);
    linked(files, openScore_, MenuIcon::Score);
    auto *practice = menu("ui.menu.practice");
    linked(practice, listen_, MenuIcon::Play);
    linked(practice, record_, MenuIcon::Microphone);
    linked(practice, stop_, MenuIcon::Stop);
    linked(practice, calibrate_, MenuIcon::Calibration);
    linked(practice, refreshMic_, MenuIcon::Refresh);
    linked(practice, replayButton_, MenuIcon::Replay);
    linked(practice, exportButton_, MenuIcon::ExportAudio);
    auto *preferences = menu("ui.menu.options");
    linked(preferences, settingsButton_, MenuIcon::Settings);
    chooseFolder_->hide();
    reload_->hide();
    openScore_->hide();
    settingsButton_->hide();
}
} // namespace singlilt
