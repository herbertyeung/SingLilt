// Main workspace widgets and playback-control layout.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "InstrumentNames.h"
#include "MainWindow.h"
#include "MenuIcons.h"
#include "ScoreView.h"
#include "StaffNoteEditor.h"
#include "i18n/LanguageManager.h"
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <algorithm>

namespace singlilt
{
namespace
{
QString clockText(double seconds)
{
    int s = std::max(0, int(seconds));
    return QString(trText("ui.format.clock")).arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

} // namespace

void MainWindow::createUi()
{
    auto *root = new QWidget;
    root->setObjectName("root");
    setCentralWidget(root);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(12, 8, 12, 6);
    layout->setSpacing(6);
    view_ = new ScoreView;
    view_->setObjectName("scoreView");
    view_->staffNoteClicked = [this](int index) { selectStaffNote(index); };
    view_->staffAnchorEditStarted = [this](int index) { beginStaffAnchorEdit(index); };
    view_->staffAnchorMoved = [this](int index, const SourceRect &anchor) { moveStaffAnchor(index, anchor); };
    view_->staffNoteAddRequested = [this](QPointF position) { addStaffNoteAt(position); };
    view_->staffNoteChordAddRequested = [this](int index, QPointF position) { addStaffNoteAt(position, index); };
    view_->staffNoteDeleteRequested = [this](int index) { deleteStaffNote(index); };
    view_->staffMusicalMoved = [this](int index, const SourceRect &anchor, bool pitch, bool timing)
    { moveStaffNote(index, anchor, pitch, timing); };
    createHeader(layout);
    auto *scoreSettings = new QWidget(root);
    scoreSettings->setObjectName("legacyScoreSettings");
    auto *scoreLayout = new QVBoxLayout(scoreSettings);
    createScoreOptions(scoreLayout);
    scoreSettings->hide();
    createAudioControls(layout);
    createCloudTaskBanner(layout);
    createLocalStaffTaskBanner(layout);
    createScorePane(layout);
    auto *audioSettings = new QWidget(root);
    audioSettings->setObjectName("legacyAudioSettings");
    auto *audioLayout = new QVBoxLayout(audioSettings);
    createPracticeControls(audioLayout);
    audioSettings->hide();
    createTransport(layout);
    createMenus();
    status_ = label("ui.status.ready");
    status_->setObjectName("statusMessage");
    statusBar()->addWidget(status_, 1);
    setStatus("ui.status.ready");
}

void MainWindow::createHeader(QVBoxLayout *layout)
{
    auto *header = new QHBoxLayout;
    title_ = new QLabel;
    title_->setObjectName("title");
    title_->setTextFormat(Qt::PlainText);
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title_->setMaximumWidth(280);
    subtitle_ = label("ui.subtitle.tagline");
    subtitle_->setObjectName("subtitle");
    projectIdentity_ = new QLabel;
    projectIdentity_->setObjectName("projectIdentity");
    projectIdentity_->setTextFormat(Qt::PlainText);
    projectIdentity_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    projectIdentity_->setMaximumWidth(280);
    header->addWidget(title_, 1);
    header->addWidget(projectIdentity_, 1);
    header->addStretch();
    header->addWidget(subtitle_);
    import_ = button("ui.header.import", header);
    import_->setObjectName("import");
    auto *paste = button("ui.header.paste", header);
    paste->setObjectName("pasteScore");
    auto *sample = button("ui.header.samples", header);
    auto *sampleMenu = new QMenu(sample);
    sampleMenu->setObjectName("practiceMenu");
    auto *scale = sampleMenu->addAction(menuIcon(MenuIcon::Scale), trText("ui.sample.scale"));
    scale->setIconVisibleInMenu(true);
    bindText(scale, "ui.sample.scale");
    auto *intro = sampleMenu->addAction(menuIcon(MenuIcon::Score), trText("ui.sample.intro"));
    intro->setIconVisibleInMenu(true);
    bindText(intro, "ui.sample.intro");
    auto *original = sampleMenu->addAction(menuIcon(MenuIcon::ImportImage), trText("ui.sample.original"));
    original->setIconVisibleInMenu(true);
    bindText(original, "ui.sample.original");
    sample->setMenu(sampleMenu);
    auto *classroom = button("ui.classroom.open", header);
    classroom->setObjectName("openSingingClassroom");
    QObject::connect(classroom, &QPushButton::clicked, this, [this] { openClassroom(); });
    auto *saveButton = button("ui.button.save", header);
    saveButton->setObjectName("saveProject");
    languageSelector_ = new QComboBox;
    languageSelector_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    languageSelector_->setObjectName("languageSelector");
    for (const auto &language : languageManager_.availableLanguages())
    {
        languageSelector_->addItem(languageManager_.languageName(language), language);
    }
    languageSelector_->setCurrentIndex(languageSelector_->findData(languageManager_.language()));
    bindText(languageSelector_, "ui.language.label", "toolTip");
    bindText(languageSelector_, "ui.language.label", "accessibleName");
    header->addWidget(languageSelector_);
    QObject::connect(languageSelector_, &QComboBox::currentIndexChanged, this,
                     [this](int index)
                     {
                         const QString language = languageSelector_->itemData(index).toString();
                         if (languageManager_.setLanguage(language))
                         {
                             QSettings settings;
                             settings.setValue("ui/language", language);
                         }
                         else
                         {
                             QSignalBlocker blocker(languageSelector_);
                             languageSelector_->setCurrentIndex(
                                 languageSelector_->findData(languageManager_.language()));
                             showError(languageManager_.errorString());
                         }
                     });
    for (auto *widget :
         std::initializer_list<QWidget *>{import_, paste, sample, classroom, saveButton, languageSelector_})
        widget->hide();
    layout->addLayout(header);
    auto *tasks = new QHBoxLayout;
    auto *song = button("ui.product.song_practice", tasks);
    song->setObjectName("songPracticeTask");
    auto *lesson = button("ui.classroom.open", tasks);
    lesson->setObjectName("classroomTask");
    auto *ear = button("ui.menu.ear", tasks);
    ear->setObjectName("earTrainingTask");
    QObject::connect(song, &QPushButton::clicked, this, [this] { setCorrectionMode(false); });
    QObject::connect(lesson, &QPushButton::clicked, this, [this] { openClassroom(); });
    QObject::connect(ear, &QPushButton::clicked, this,
                     [this]
                     {
                         if (auto *action = findChild<QAction *>("menuEarTraining"))
                             action->trigger();
                     });
    tasks->addStretch();
    staffPageToolbar_ = new QWidget;
    staffPageToolbar_->setObjectName("staffPageToolbar");
    auto *pageRow = new QHBoxLayout(staffPageToolbar_);
    pageRow->setContentsMargins(0, 0, 0, 0);
    staffPreviousPage_ = new QPushButton(trText("ui.pages.previous"));
    staffPreviousPage_->setObjectName("staffPreviousPage");
    bindText(staffPreviousPage_, "ui.pages.previous");
    staffPreviousPage_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    pageRow->addWidget(staffPreviousPage_);
    staffPageSelector_ = new QComboBox;
    staffPageSelector_->setObjectName("staffPageSelector");
    pageRow->addWidget(staffPageSelector_);
    staffNextPage_ = new QPushButton(trText("ui.pages.next"));
    staffNextPage_->setObjectName("staffNextPage");
    bindText(staffNextPage_, "ui.pages.next");
    staffNextPage_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    pageRow->addWidget(staffNextPage_);
    staffAutoPage_ = new QCheckBox(trText("ui.pages.auto_turn"));
    staffAutoPage_->setObjectName("staffAutoPage");
    bindText(staffAutoPage_, "ui.pages.auto_turn");
    staffAutoPage_->setChecked(true);
    pageRow->addWidget(staffAutoPage_);
    pageRow->addStretch();
    connect(staffPreviousPage_, &QPushButton::clicked, this, [this] { setStaffPage(staffPageIndex_ - 1); });
    connect(staffNextPage_, &QPushButton::clicked, this, [this] { setStaffPage(staffPageIndex_ + 1); });
    connect(staffPageSelector_, &QComboBox::currentIndexChanged, this,
            [this](int index)
            {
                if (!loading_)
                    setStaffPage(index);
            });
    for (QWidget *control :
         std::initializer_list<QWidget *>{staffPreviousPage_, staffPageSelector_, staffNextPage_, staffAutoPage_})
        control->hide();
    auto *modeSwitch = new QWidget;
    modeSwitch->setObjectName("workspaceMode");
    auto *modeLayout = new QHBoxLayout(modeSwitch);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(2);
    practiceModeButton_ = button("ui.product.practice_mode", modeLayout);
    practiceModeButton_->setObjectName("practiceModeButton");
    practiceModeButton_->setCheckable(true);
    practiceModeButton_->setProperty("modeSwitch", true);
    correctionModeButton_ = button("ui.product.correction_mode", modeLayout);
    correctionModeButton_->setObjectName("correctionModeButton");
    correctionModeButton_->setCheckable(true);
    correctionModeButton_->setProperty("modeSwitch", true);
    auto *modeGroup = new QButtonGroup(modeSwitch);
    modeGroup->setExclusive(true);
    modeGroup->addButton(practiceModeButton_, 0);
    modeGroup->addButton(correctionModeButton_, 1);
    practiceModeButton_->setChecked(true);
    connect(modeGroup, &QButtonGroup::idClicked, this, [this](int id) { setCorrectionMode(id == 1); });
    tasks->addWidget(modeSwitch);
    auto *staffAdd = button("ui.original_playback.add_note", tasks);
    staffAdd->setObjectName("staffImageAddNote");
    auto *staffDelete = button("ui.original_playback.delete_note", tasks);
    staffDelete->setObjectName("staffImageDeleteNote");
    connect(staffAdd, &QPushButton::clicked, this,
            [this] { addStaffNoteAt(view_->mapToScene(view_->viewport()->rect().center())); });
    connect(staffDelete, &QPushButton::clicked, this, [this] { deleteStaffNote(selectedStaffNote_); });
    auto *audition = button("ui.original_playback.audition_note", tasks);
    audition->setObjectName("staffImageNoteAudition");
    connect(audition, &QPushButton::clicked, this,
            [this]
            {
                if (canEditOriginalStaff() && selectedStaffNote_ >= 0 &&
                    selectedStaffNote_ < int(project_.staffPerformance->notes.size()))
                    auditionStaffNote(project_.staffPerformance->notes[std::size_t(selectedStaffNote_)]);
            });
    auto *pitchDrag = new QCheckBox(trText("ui.original_playback.pitch_drag"));
    pitchDrag->setObjectName("staffPitchDrag");
    bindText(pitchDrag, "ui.original_playback.pitch_drag");
    pitchDrag->setChecked(true);
    tasks->addWidget(pitchDrag);
    connect(pitchDrag, &QCheckBox::toggled, this, [this](bool) { refreshStaffAnchorEditing(); });
    auto *timingDrag = new QCheckBox;
    timingDrag->setObjectName("staffTimingDrag");
    bindText(timingDrag, "ui.original_playback.timing_drag");
    timingDrag->setChecked(true);
    tasks->addWidget(timingDrag);
    connect(timingDrag, &QCheckBox::toggled, this, [this](bool) { refreshStaffAnchorEditing(); });
    notationStyle_ = new QComboBox;
    notationStyle_->setObjectName("notationStyle");
    notationStyle_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    addTranslatedItem(notationStyle_, "ui.staff.original_view", -1);
    addTranslatedItem(notationStyle_, "ui.staff.numbered_view", int(NotationStyle::Numbered));
    addTranslatedItem(notationStyle_, "ui.staff.staff_view", int(NotationStyle::Staff));
    tasks->addWidget(notationStyle_);
    QObject::connect(notationStyle_, &QComboBox::currentIndexChanged, this,
                     [this](int index)
                     {
                         const int style = notationStyle_->itemData(index).toInt();
                         if (!loading_ && style >= 0)
                             setNotationStyle(static_cast<NotationStyle>(style));
                     });
    reviewStatus_ = new QLabel;
    reviewStatus_->setObjectName("reviewStatus");
    tasks->addWidget(reviewStatus_);
    auto *next = button("ui.product.next_review", tasks);
    next->setObjectName("nextUncertainNote");
    QObject::connect(next, &QPushButton::clicked, this, [this] { nextUncertainNote(); });
    layout->addLayout(tasks);
    layout->addWidget(staffPageToolbar_);
    staffPageToolbar_->hide();
    QObject::connect(import_, &QPushButton::clicked, this,
                     [this]
                     {
                         const auto path = chooseFile(false);
                         if (!path.isEmpty())
                             openFile(path);
                     });
    QObject::connect(paste, &QPushButton::clicked, this,
                     [this]
                     {
                         if (busy_)
                             return;
                         auto image = QApplication::clipboard()->image();
                         if (image.isNull())
                         {
                             showError(QStringLiteral("ui.error.clipboard"));
                             return;
                         }
                         if (confirmDiscard())
                             recognize(image, "clipboard");
                     });
    QObject::connect(scale, &QAction::triggered, this,
                     [this]
                     {
                         if (!busy_ && confirmDiscard())
                         {
                             try
                             {
                                 setProject(makePracticeScore());
                             }
                             catch (const std::exception &error)
                             {
                                 showError(QString::fromUtf8(error.what()));
                             }
                         }
                     });
    QObject::connect(saveButton, &QPushButton::clicked, this, [this] { save(); });

    QObject::connect(intro, &QAction::triggered, this, [this] { openPracticeExample(false); });
    QObject::connect(original, &QAction::triggered, this, [this] { openPracticeExample(true); });
}

void MainWindow::createScoreOptions(QVBoxLayout *layout)
{
    auto *options = new QHBoxLayout;
    options->addWidget(label("ui.option.key"));
    key_ = new QComboBox;
    for (int i = 0; i < 12; ++i)
        addTranslatedItem(key_, qPrintable(QString("ui.key.%1").arg(i)), i);
    key_->setObjectName("scoreKey");
    key_->setMinimumWidth(108);
    options->addWidget(key_);
    options->addSpacing(12);
    options->addWidget(label("ui.option.meter"));
    meterTop_ = new QSpinBox;
    meterTop_->setObjectName("meterTop");
    meterTop_->setRange(1, MaximumBeatsPerBar);
    meterTop_->setFixedWidth(58);
    options->addWidget(meterTop_);
    options->addWidget(label("ui.option.meter_separator"));
    meterBottom_ = new QComboBox;
    for (int unit = 1; unit <= MaximumBeatUnit; unit *= 2)
        meterBottom_->addItem(QString::number(unit), unit);
    meterBottom_->setObjectName("meterBottom");
    options->addWidget(meterBottom_);
    options->addSpacing(12);
    options->addWidget(label("ui.option.tempo"));
    tempo_ = new QDoubleSpinBox;
    tempo_->setObjectName("tempo");
    tempo_->setRange(MinimumScoreBpm, MaximumScoreBpm);
    tempo_->setDecimals(1);
    tempo_->setFixedWidth(80);
    options->addWidget(tempo_);
    options->addSpacing(12);
    options->addWidget(label("ui.option.transpose"));
    transpose_ = new QSpinBox;
    transpose_->setObjectName("transpose");
    transpose_->setRange(-24, 24);
    bindText(transpose_, "ui.option.semitones", "suffix");
    options->addWidget(transpose_);
    options->addSpacing(12);
    options->addWidget(label("ui.option.velocity"));
    velocity_ = new QSpinBox;
    velocity_->setObjectName("noteVelocity");
    velocity_->setRange(1, 127);
    velocity_->setFixedWidth(65);
    bindText(velocity_, "ui.option.velocity_tip", "toolTip");
    options->addWidget(velocity_);
    accentBeats_ = checkBox("ui.option.accent");
    accentBeats_->setObjectName("accentBeats");
    options->addWidget(accentBeats_);
    auto changePerformance = [this]
    {
        if (loading_ || project_.staffPerformance || project_.score.notes.empty())
            return;
        project_.score.baseVelocity = velocity_->value();
        project_.score.accentBeats = accentBeats_->isChecked();
        markModified();
        rebuild(true);
    };
    QObject::connect(velocity_, &QSpinBox::valueChanged, this, changePerformance);
    QObject::connect(accentBeats_, &QCheckBox::toggled, this, changePerformance);
    options->addStretch();
    auto *fit = button("ui.option.fit", options);
    auto *flags = checkBox("ui.option.flags");
    flags->setChecked(true);
    options->addWidget(flags);
    layout->addLayout(options);
    QObject::connect(key_, &QComboBox::currentIndexChanged, this,
                     [this](int i)
                     {
                         if (!loading_)
                         {
                             if (project_.staffPerformance && !project_.staffImagePlayback &&
                                 project_.notationStyle == NotationStyle::Numbered)
                             {
                                 updateNumberedMetadata();
                                 return;
                             }
                             project_.score.tonic = key_->itemData(i).toInt();
                             markModified();
                             rebuild(true);
                         }
                     });
    QObject::connect(tempo_, &QDoubleSpinBox::valueChanged, this,
                     [this](double v)
                     {
                         if (!loading_)
                         {
                             project_.score.bpm = v;
                             markModified();
                             rebuild(true);
                         }
                     });
    auto meterChanged = [this]
    {
        if (!loading_)
        {
            if (project_.staffPerformance && !project_.staffImagePlayback &&
                project_.notationStyle == NotationStyle::Numbered)
            {
                updateNumberedMetadata();
                return;
            }
            project_.score.beatsPerBar = meterTop_->value();
            project_.score.beatUnit = meterBottom_->currentData().toInt();
            markModified();
            rebuild(true);
        }
    };
    QObject::connect(meterTop_, &QSpinBox::valueChanged, this, meterChanged);
    QObject::connect(meterBottom_, &QComboBox::currentIndexChanged, this, meterChanged);
    QObject::connect(transpose_, &QSpinBox::valueChanged, this,
                     [this](int v)
                     {
                         player_.setTranspose(v);
                         if (!loading_)
                             markModified();
                         QSignalBlocker blocker(transpose_);
                         transpose_->setValue(player_.transpose());
                         if (!player_.errorString().isEmpty())
                             setStatusMessage(player_.errorString());
                     });
    QObject::connect(fit, &QPushButton::clicked, this, [this] { displayedScoreView()->fitWidth(); });
    QObject::connect(flags, &QCheckBox::toggled, view_, &ScoreView::showUncertain);
}

QWidget *MainWindow::createInspector()
{
    auto *inspector = new QFrame;
    inspector->setObjectName("inspector");
    inspector->setMinimumWidth(264);
    inspector->setMaximumWidth(330);
    auto *side = new QVBoxLayout(inspector);
    side->setSizeConstraint(QLayout::SetMinimumSize);
    side->setContentsMargins(16, 16, 16, 12);
    noteTitle_ = label("ui.inspector.title");
    noteTitle_->setObjectName("noteTitle");
    noteTitle_->setWordWrap(true);
    side->addWidget(noteTitle_);
    auto *hint = label("ui.inspector.hint");
    hint->setWordWrap(true);
    hint->setObjectName("inspectorHint");
    side->addWidget(hint);
    auto *form = new QFormLayout;
    degree_ = new QComboBox;
    for (int i = 0; i < 8; ++i)
        addTranslatedItem(degree_, qPrintable(QString("ui.note.degree.%1").arg(i)), i);
    degree_->setObjectName("noteDegree");
    form->addRow(label("ui.inspector.degree"), degree_);
    octave_ = new QSpinBox;
    octave_->setObjectName("noteOctave");
    octave_->setRange(-4, 4);
    form->addRow(label("ui.inspector.octave"), octave_);
    accidental_ = new QSpinBox;
    accidental_->setObjectName("noteAccidental");
    accidental_->setRange(-2, 2);
    form->addRow(label("ui.inspector.accidental"), accidental_);
    duration_ = new QDoubleSpinBox;
    duration_->setObjectName("noteDuration");
    duration_->setRange(1.0 / TicksPerQuarter, double(MaximumNoteDurationTicks) / TicksPerQuarter);
    duration_->setSingleStep(.125);
    duration_->setDecimals(6);
    form->addRow(label("ui.inspector.duration"), duration_);
    noteKey_ = new QComboBox;
    addTranslatedItem(noteKey_, "ui.inspector.inherit_key", -1);
    for (int i = 0; i < 12; ++i)
        addTranslatedItem(noteKey_, qPrintable(QString("ui.key.%1").arg(i)), i);
    form->addRow(label("ui.inspector.key"), noteKey_);
    lyricEdit_ = new QLineEdit;
    lyricEdit_->setObjectName("lyricAEditor");
    bindText(lyricEdit_, "ui.inspector.placeholder_a", "placeholderText");
    form->addRow(label("ui.inspector.lyric_a"), lyricEdit_);
    lyricBEdit_ = new QLineEdit;
    lyricBEdit_->setObjectName("lyricBEditor");
    bindText(lyricBEdit_, "ui.inspector.placeholder_b", "placeholderText");
    form->addRow(label("ui.inspector.lyric_b"), lyricBEdit_);
    sharedLyric_ = checkBox("ui.inspector.shared");
    sharedLyric_->setObjectName("sharedLyric");
    form->addRow(sharedLyric_);
    QObject::connect(sharedLyric_, &QCheckBox::toggled, lyricBEdit_, &QWidget::setDisabled);
    tie_ = checkBox("ui.inspector.tie");
    tie_->setObjectName("noteTie");
    form->addRow(tie_);
    side->addLayout(form);
    auto *noteButtons = new QHBoxLayout;
    auto *apply = button("ui.inspector.apply", noteButtons);
    apply->setObjectName("applyNoteChanges");
    auto *remove = button("ui.inspector.remove", noteButtons);
    side->addLayout(noteButtons);
    QObject::connect(apply, &QPushButton::clicked, this, [this] { updateNote(); });
    remove->setObjectName("removeNote");
    QObject::connect(remove, &QPushButton::clicked, this, [this] { removeNote(); });
    auto *repeat = button("ui.inspector.repeats", side);
    QObject::connect(repeat, &QPushButton::clicked, this, [this] { editRepeats(); });
    side->addSpacing(8);
    side->addWidget(label("ui.inspector.validation"));
    issues_ = new QPlainTextEdit;
    issues_->setObjectName("issues");
    issues_->setReadOnly(true);
    issues_->setMinimumHeight(60);
    side->addWidget(issues_, 1);
    cloud_ = button("ui.inspector.cloud", side);
    QObject::connect(cloud_, &QPushButton::clicked, this, [this] { configureCloud(); });
    cloud_->hide();
    auto *raw = button("ui.inspector.raw", side);
    QObject::connect(raw, &QPushButton::clicked, this,
                     [this]
                     {
                         QDialog d(this);
                         bindText(&d, "ui.dialog.raw_title", "windowTitle");
                         d.resize(700, 550);
                         QVBoxLayout l(&d);
                         QPlainTextEdit text;
                         text.setReadOnly(true);
                         if (debugText_.isEmpty())
                             bindText(&text, "ui.dialog.no_raw", "plainText");
                         else
                             text.setPlainText(debugText_);
                         l.addWidget(&text);
                         d.exec();
                     });
    return inspector;
}

void MainWindow::createScorePane(QVBoxLayout *layout)
{
    auto *body = new QSplitter;
    staffScoreTabs_ = new QTabWidget;
    staffScoreTabs_->setObjectName("staffScoreTabs");
    staffScoreTabs_->addTab(view_, menuIcon(MenuIcon::Score), trText("ui.fidelity.recognized"));
    staffSourceView_ = new ScoreView;
    staffSourceView_->setObjectName("staffSourceView");
    staffSourceView_->setBackgroundBrush(Qt::NoBrush);
    staffScoreTabs_->addTab(staffSourceView_, menuIcon(MenuIcon::ImportImage), trText("ui.fidelity.original"));
    staffScoreTabs_->setTabVisible(1, false);
    staffScoreTabs_->tabBar()->hide();
    connect(staffScoreTabs_, &QTabWidget::currentChanged, this, [this](int) { displayedScoreView()->fitWidth(); });
    body->addWidget(staffScoreTabs_);
    auto *inspectorHost = new QWidget;
    inspectorHost->setObjectName("inspectorScroll");
    inspectorHost->setMinimumWidth(284);
    inspectorHost->setMaximumWidth(350);
    auto *inspectorLayout = new QVBoxLayout(inspectorHost);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    noteInspectorStack_ = new QStackedWidget;
    noteInspectorStack_->setObjectName("noteInspectorStack");
    // Each page owns its overflow; a hidden, taller inspector must not stretch the active page.
    auto *legacyScroll = new QScrollArea;
    legacyScroll->setObjectName("legacyInspectorScroll");
    legacyScroll->setFrameShape(QFrame::NoFrame);
    legacyScroll->setWidgetResizable(true);
    legacyScroll->setWidget(createInspector());
    noteInspectorStack_->addWidget(legacyScroll);
    staffNoteEditor_ = new StaffNoteEditor;
    staffNoteEditor_->setObjectName("staffNoteInspector");
    auto *staffScroll = new QScrollArea;
    staffScroll->setObjectName("staffInspectorScroll");
    staffScroll->setFrameShape(QFrame::NoFrame);
    staffScroll->setWidgetResizable(true);
    staffScroll->setWidget(staffNoteEditor_);
    noteInspectorStack_->addWidget(staffScroll);
    staffNoteEditor_->draftPreviewChanged = [this](const StaffPerformanceNote &note)
    {
        if (!canEditOriginalStaff(true) || selectedStaffNote_ < 0 ||
            selectedStaffNote_ >= int(project_.staffPerformance->notes.size()))
            return;
        const auto &before = project_.staffPerformance->notes[std::size_t(selectedStaffNote_)];
        previewStaffInspectorNote(before, note);
    };
    staffNoteEditor_->auditionRequested = [this](const auto &note)
    {
        if (canEditOriginalStaff(true))
            auditionStaffNote(note);
    };
    staffNoteEditor_->applyRequested = [this] { applyStaffInspectorNote(); };
    staffNoteEditor_->cancelRequested = [this] { cancelStaffInspectorNote(); };
    staffNoteEditor_->deleteRequested = [this]
    {
        if (resolveNoteDraft())
            deleteStaffNote(selectedStaffNote_);
    };
    staffNoteEditor_->draftChanged = [this] { refreshStaffAnchorEditing(); };
    inspectorLayout->addWidget(noteInspectorStack_);
    inspectorScroll_ = inspectorHost;
    body->addWidget(inspectorHost);
    inspectorHost->hide();
    body->setStretchFactor(0, 1);
    body->setStretchFactor(1, 0);
    body->setSizes({940, 350});
    layout->addWidget(body, 1);
    view_->noteClicked = [this](int i) { selectNote(i); };
    view_->emptyDoubleClicked = [this](QPointF position) { insertNote(position); };
}

void MainWindow::createTransport(QVBoxLayout *layout)
{
    // Keep compatibility controls available to Options without crowding practice.
    auto *voiceSettings = new QWidget(centralWidget());
    voiceSettings->setObjectName("legacyVoiceSettings");
    auto *voiceLayout = new QHBoxLayout(voiceSettings);
    verseView_ = new QComboBox;
    verseView_->setObjectName("verseSelector");
    addTranslatedItem(verseView_, "ui.transport.verse_a", 0);
    addTranslatedItem(verseView_, "ui.transport.verse_b", 1);
    voiceLayout->addWidget(verseView_);
    verseStatus_ = new QLabel;
    verseStatus_->setObjectName("verseStatus");
    voiceLayout->addWidget(verseStatus_);
    audioSource_ = new QComboBox;
    audioSource_->setObjectName("audioBackend");
    addTranslatedItem(audioSource_, "ui.transport.source_piano", 0);
    addTranslatedItem(audioSource_, "ui.transport.source_midi", 1);
    voiceLayout->addWidget(audioSource_);
    voiceSettings->hide();
    QObject::connect(audioSource_, &QComboBox::currentIndexChanged, this,
                     [this](int index)
                     {
                         if (audioLoading_)
                             return;
                         playIntent_ = false;
                         const auto backend = index == 1 ? AudioBackend::WindowsMidi : AudioBackend::SampledPiano;
                         if (!player_.setAudioBackend(backend))
                         {
                             QSignalBlocker blocker(audioSource_);
                             audioSource_->setCurrentIndex(
                                 player_.audioBackend() == AudioBackend::WindowsMidi ? 1 : 0);
                             showError(player_.errorString());
                             return;
                         }
                         QSettings().setValue("audio/backend", index);
                         setStatus(index == 0 ? "ui.status.source_piano" : "ui.status.source_midi");
                     });
    QObject::connect(verseView_, &QComboBox::currentIndexChanged, this,
                     [this](int index)
                     {
                         if (!loading_)
                             seekVerse(verseView_->itemData(index).toInt());
                     });
    programA_ = new QComboBox;
    programB_ = new QComboBox;
    programA_->setObjectName("programA");
    programB_->setObjectName("programB");
    for (auto *program : {programA_, programB_})
    {
        for (const auto &instrument : CommonInstruments)
            addTranslatedItem(program, instrument.key, instrument.program);
        bindText(program, "ui.transport.program_tip", "toolTip");
        program->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        program->setMinimumContentsLength(10);
        program->setMaximumWidth(210);
    }
    QObject::connect(programA_, &QComboBox::currentIndexChanged, this, [this] { updateVersePrograms(); });
    QObject::connect(programB_, &QComboBox::currentIndexChanged, this, [this] { updateVersePrograms(); });
    lyric_ = label("ui.transport.ready");
    lyric_->setObjectName("lyric");
    lyric_->setTextFormat(Qt::PlainText);
    lyric_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    lyric_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    lyric_->setMinimumWidth(100);
    lyric_->setMaximumWidth(240);
    auto *progress = new QHBoxLayout;
    progress->addWidget(lyric_);
    position_ = new QSlider(Qt::Horizontal);
    position_->setObjectName("position");
    progress->addWidget(position_, 1);
    time_ = new QLabel(trText("ui.format.range").arg(clockText(0), clockText(0)));
    time_->setObjectName("playbackTime");
    time_->setMinimumWidth(100);
    progress->addWidget(time_);
    layout->addLayout(progress);
    QObject::connect(position_, &QSlider::sliderReleased, this, [this] { seekPracticeTick(position_->value()); });

    auto *transport = new QHBoxLayout;
    play_ = button("ui.transport.play", transport);
    play_->setObjectName("play");
    auto *stop = button("ui.transport.stop", transport);
    stop->setObjectName("stop");
    QObject::connect(play_, &QPushButton::clicked, this, [this] { togglePlayback(); });
    QObject::connect(stop, &QPushButton::clicked, this,
                     [this]
                     {
                         restorePendingOriginalSource_ = {};
                         if (originalAudio_.isLoading())
                         {
                             originalAudio_.cancelOpen();
                             const QSignalBlocker blocker(playbackSource_);
                             playbackSource_->setCurrentIndex(
                                 std::max(0, playbackSource_->findData(previousOriginalSource_)));
                             refreshSourceControls();
                         }
                         playIntent_ = false;
                         player_.stop();
                         originalAudio_.stop();
                         updatePlayback();
                     });
    metronome_ = checkBox("ui.transport.metronome");
    metronome_->setObjectName("metronome");
    transport->addWidget(metronome_);
    QObject::connect(metronome_, &QCheckBox::toggled, this,
                     [this](bool enabled)
                     {
                         player_.setMetronome(enabled);
                         if (!loading_)
                         {
                             settings_.metronome = enabled;
                             QSettings().setValue("audio/metronome", enabled);
                             markModified();
                         }
                     });
    transport->addStretch();
    auto *programALabel = label("ui.transport.program_a");
    programALabel->setObjectName("programALabel");
    transport->addWidget(programALabel);
    transport->addWidget(programA_);
    auto *programBLabel = label("ui.transport.program_b");
    programBLabel->setObjectName("programBLabel");
    transport->addWidget(programBLabel);
    transport->addWidget(programB_);
    transport->insertWidget(3, label("ui.product.speed"));
    practiceSpeed_ = new QDoubleSpinBox;
    practiceSpeed_->setObjectName("practiceSpeed");
    practiceSpeed_->setRange(.25, 2.0);
    practiceSpeed_->setDecimals(2);
    practiceSpeed_->setSingleStep(.1);
    practiceSpeed_->setSuffix("x");
    practiceSpeed_->setValue(1);
    practiceSpeed_->setMaximumWidth(88);
    transport->insertWidget(4, practiceSpeed_);
    QObject::connect(practiceSpeed_, &QDoubleSpinBox::valueChanged, this,
                     [this](double speed)
                     {
                         if (loading_)
                             return;
                         if (playbackSource_->currentData().toInt() > 0)
                             findChild<QDoubleSpinBox *>("originalSpeed")->setValue(speed);
                         else
                             player_.setSpeed(speed);
                         QSignalBlocker blocker(practiceSpeed_);
                         practiceSpeed_->setValue(playbackSource_->currentData().toInt() > 0
                                                      ? findChild<QDoubleSpinBox *>("originalSpeed")->value()
                                                      : player_.speed());
                         markModified();
                     });
    transport->insertWidget(5, label("ui.option.transpose"));
    transport->insertWidget(6, transpose_);
    transpose_->show();
    layout->addLayout(transport);
    auto *loopControls = new QHBoxLayout;
    auto *a = button("ui.transport.loop_start", loopControls);
    a->setObjectName("practiceLoopStart");
    auto *b = button("ui.transport.loop_end", loopControls);
    b->setObjectName("practiceLoopEnd");
    loop_ = checkBox("ui.transport.loop");
    loop_->setObjectName("practiceLoop");
    loopControls->addWidget(loop_);
    QObject::connect(a, &QPushButton::clicked, this,
                     [this]
                     {
                         loopStart_ = practicePositionTick();
                         ++loopEditRevision_;
                         markModified();
                         setStatus("ui.status.loop_start", {clockText(timeline_.secondsAtTick(loopStart_))});
                     });
    QObject::connect(b, &QPushButton::clicked, this,
                     [this]
                     {
                         loopEnd_ = practicePositionTick();
                         ++loopEditRevision_;
                         markModified();
                         setStatus("ui.status.loop_end", {clockText(timeline_.secondsAtTick(loopEnd_))});
                     });
    QObject::connect(loop_, &QCheckBox::toggled, this,
                     [this](bool on)
                     {
                         if (!loading_)
                         {
                             ++loopEditRevision_;
                             markModified();
                         }
                         if (on && loopEnd_ <= loopStart_)
                         {
                             loopStart_ = 0;
                             loopEnd_ = timeline_.durationTicks;
                         }
                     });
    auto *mix = new QHBoxLayout;
    mix->addWidget(label("ui.product.source"));
    playbackSource_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    playbackSource_->setMinimumContentsLength(17);
    playbackSource_->setMaximumWidth(210);
    mix->addWidget(playbackSource_);
    playbackSource_->show();
    mix->addWidget(melodyEnabled_);
    melodyEnabled_->show();
    volume_ = new QSlider(Qt::Horizontal);
    volume_->setObjectName("outputVolume");
    volume_->setRange(0, 100);
    volume_->setValue(90);
    volume_->setMinimumWidth(60);
    volume_->setMaximumWidth(90);
    bindText(volume_, "ui.transport.volume_tip", "toolTip");
    mix->addWidget(volume_);
    auto *volumeText = new QLabel(trText("ui.format.percent").arg(volume_->value()));
    volumeText->setObjectName("outputVolumePercent");
    mix->addWidget(volumeText);
    QObject::connect(volume_, &QSlider::valueChanged, this,
                     [this, volumeText](int value)
                     {
                         volumeText->setText(trText("ui.format.percent").arg(value));
                         if (playbackSource_->currentData().toInt() > 0)
                         {
                             findChild<QSlider *>("originalVolume")->setValue(value);
                             markModified();
                         }
                         else
                             updatePracticeMix();
                     });
    mix->addWidget(accompanimentEnabled_);
    accompanimentEnabled_->show();
    mix->addWidget(accompanimentVolume_);
    accompanimentVolume_->show();
    mix->addStretch();
    mix->addLayout(loopControls);
    auto *generate = button("ui.product.accompaniment", mix);
    generate->setObjectName("quickGenerateAccompaniment");
    QObject::connect(generate, &QPushButton::clicked, this, [this] { generatePracticeAccompaniment(); });
    layout->addLayout(mix);
    layout->addWidget(accompanimentState_);
    accompanimentState_->show();
    connect(playbackSource_, &QComboBox::currentIndexChanged, this, [this] { refreshSourceControls(); });
}

void MainWindow::refreshSourceControls()
{
    const bool original = playbackSource_->currentData().toInt() > 0;
    const QSignalBlocker speedBlock(practiceSpeed_), volumeBlock(volume_);
    practiceSpeed_->setValue(original ? findChild<QDoubleSpinBox *>("originalSpeed")->value() : player_.speed());
    volume_->setValue(original ? findChild<QSlider *>("originalVolume")->value()
                               : int(project_.practiceMix.melodyVolume * 100));
    transpose_->setEnabled(!original);
    if (original)
        bindText(transpose_, "ui.product.original_transpose", "toolTip");
    else
    {
        transpose_->setProperty("_ui_toolTip", QVariant());
        transpose_->setToolTip(QString());
    }
    findChild<QLabel *>("outputVolumePercent")->setText(trText("ui.format.percent").arg(volume_->value()));
}

} // namespace singlilt
