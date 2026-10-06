// Recognition-candidate comparison and acceptance.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "RecognitionPreviewDialog.h"
#include "ScoreView.h"
#include "i18n/LanguageManager.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFont>
#include <QGraphicsRectItem>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdlib>
#include <set>
#include <utility>

namespace singlilt
{
RecognitionPreviewDialog::RecognitionPreviewDialog(Project project, QString sourceLabel, QWidget *parent,
                                                   QImage originalImage)
    : QDialog(parent), project_(std::move(project)), originalImage_(std::move(originalImage)),
      sourceLabel_(std::move(sourceLabel))
{
    if (!project_.staffPages.empty())
        originalImage_ = project_.staffPages.front().sourceImage;
    setObjectName("recognitionPreview");
    setModal(false);
    resize(1220, 820);
    setMinimumSize(900, 640);
    createUi();
    populateNotes();
    retranslateUi();
}

void RecognitionPreviewDialog::createUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);

    source_ = new QLabel(this);
    source_->setObjectName("previewSource");
    source_->setTextFormat(Qt::PlainText);
    source_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    source_->setWordWrap(true);
    layout->addWidget(source_);

    title_ = new QLabel(this);
    title_->setObjectName("previewTitle");
    title_->setTextFormat(Qt::PlainText);
    title_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    title_->setWordWrap(true);
    QFont titleFont = title_->font();
    titleFont.setBold(true);
    title_->setFont(titleFont);
    layout->addWidget(title_);

    auto *metadata = new QHBoxLayout;
    summary_ = new QLabel(this);
    summary_->setObjectName("previewSummary");
    summary_->setTextFormat(Qt::PlainText);
    summary_->setWordWrap(true);
    metadata->addWidget(summary_, 1);
    if (!project_.staffPages.empty())
    {
        pageSelector_ = new QComboBox(this);
        pageSelector_->setObjectName("staffPreviewPageSelector");
        for (std::size_t index = 0; index < project_.staffPages.size(); ++index)
            pageSelector_->addItem(trText("ui.pages.page_number").arg(index + 1).arg(project_.staffPages.size()),
                                   int(index));
        metadata->addWidget(pageSelector_);
        connect(pageSelector_, &QComboBox::currentIndexChanged, this, [this](int index) { setPage(index); });
    }
    fitWidth_ = new QPushButton(this);
    fitWidth_->setObjectName("previewFitWidth");
    metadata->addWidget(fitWidth_);
    layout->addLayout(metadata);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    scoreView_ = new ScoreView(splitter);
    scoreView_->setObjectName("previewScoreView");
    scoreView_->setScore(project_.image, project_.score, 0, project_.staffImagePlayback);
    if (project_.staffPerformance)
    {
        scoreView_->setStaffPerformance(&*project_.staffPerformance);
        staffSelection_ = scoreView_->scene()->addRect({}, QPen(QColor("#2463EB"), 2), QColor(36, 99, 235, 55));
        staffSelection_->setZValue(11);
        staffSelection_->setData(0, QStringLiteral("staffPreviewSelection"));
        staffSelection_->hide();
    }
    scoreView_->showUncertain(true);
    const bool hasOriginal =
        !originalImage_.isNull() || std::any_of(project_.staffPages.begin(), project_.staffPages.end(),
                                                [](const auto &page) { return !page.sourceImage.isNull(); });
    if (hasOriginal && !project_.staffImagePlayback)
    {
        imageTabs_ = new QTabWidget(splitter);
        imageTabs_->setObjectName("localStaffImageTabs");
        imageTabs_->addTab(scoreView_, trText("ui.local_staff.result_image"));
        originalView_ = new ScoreView(imageTabs_);
        originalView_->setObjectName("localStaffOriginalView");
        originalView_->setScore(originalImage_, Score{});
        imageTabs_->addTab(originalView_, trText("ui.local_staff.original_image"));
        connect(imageTabs_, &QTabWidget::currentChanged, this,
                [this](int index) { (index == 1 ? originalView_ : scoreView_)->fitWidth(); });
        if (project_.processing.value("local").toBool() && !originalImage_.isNull())
            imageTabs_->setCurrentIndex(1);
        splitter->addWidget(imageTabs_);
    }
    else
        splitter->addWidget(scoreView_);

    notes_ = new QTableWidget(0, 8, splitter);
    notes_->setObjectName("previewNotes");
    notes_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    notes_->setSelectionBehavior(QAbstractItemView::SelectRows);
    notes_->setSelectionMode(QAbstractItemView::SingleSelection);
    notes_->setSortingEnabled(false);
    notes_->setAlternatingRowColors(true);
    notes_->setWordWrap(false);
    notes_->verticalHeader()->hide();
    for (int column : {0, 1, 2, 3, 6, 7})
        notes_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    notes_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    notes_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    splitter->addWidget(notes_);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({660, 500});
    layout->addWidget(splitter, 1);

    warningsTitle_ = new QLabel(this);
    warningsTitle_->setTextFormat(Qt::PlainText);
    layout->addWidget(warningsTitle_);
    warnings_ = new QPlainTextEdit(this);
    warnings_->setObjectName("previewWarnings");
    warnings_->setReadOnly(true);
    warnings_->setMinimumHeight(60);
    warnings_->setMaximumHeight(125);
    layout->addWidget(warnings_);

    if (project_.processing.value("tempoNeedsConfirmation").toBool())
    {
        auto *tempoRow = new QHBoxLayout;
        tempoReviewLabel_ = new QLabel(this);
        tempoReviewLabel_->setWordWrap(true);
        tempoRow->addWidget(tempoReviewLabel_, 1);
        tempoReview_ = new QDoubleSpinBox(this);
        tempoReview_->setObjectName("staffTempoReview");
        tempoReview_->setDecimals(1);
        tempoReview_->setRange(MinimumScoreBpm, MaximumScoreBpm);
        const auto suggested = project_.processing.value("tempoSuggestion");
        tempoReview_->setValue(suggested.isDouble() ? suggested.toDouble() : project_.score.bpm);
        tempoRow->addWidget(tempoReview_);
        tempoConfirmed_ = new QCheckBox(this);
        tempoConfirmed_->setObjectName("staffTempoConfirmed");
        tempoRow->addWidget(tempoConfirmed_);
        layout->addLayout(tempoRow);
        connect(tempoConfirmed_, &QCheckBox::toggled, this, [this](bool) { setApplyEnabled(applyAllowed_); });
        connect(tempoReview_, &QDoubleSpinBox::valueChanged, this,
                [this](double) { tempoConfirmed_->setChecked(false); });
    }

    footer_ = new QLabel(this);
    footer_->setObjectName("previewNotice");
    footer_->setTextFormat(Qt::PlainText);
    footer_->setWordWrap(true);
    footer_->setFrameShape(QFrame::StyledPanel);
    footer_->setMargin(10);
    QFont noticeFont = footer_->font();
    noticeFont.setBold(true);
    footer_->setFont(noticeFont);
    layout->addWidget(footer_);

    auto *actions = new QHBoxLayout;
    actions->addStretch();
    close_ = new QPushButton(this);
    close_->setObjectName("recognitionClose");
    actions->addWidget(close_);
    apply_ = new QPushButton(this);
    apply_->setObjectName("recognitionApply");
    actions->addWidget(apply_);
    layout->addLayout(actions);

    QObject::connect(
        fitWidth_, &QPushButton::clicked, this,
        [this] { (imageTabs_ && imageTabs_->currentIndex() == 1 ? originalView_ : scoreView_)->fitWidth(); });
    QObject::connect(close_, &QPushButton::clicked, this, &QWidget::close);
    QObject::connect(apply_, &QPushButton::clicked, this,
                     [this]
                     {
                         // The owner may close this dialog while applying the result.
                         const auto request = applyRequested;
                         if (request)
                             request();
                     });
    if (project_.staffPerformance)
        scoreView_->staffNoteClicked = [this](int index) { selectStaffNote(index, false); };
    else
        scoreView_->noteClicked = [this](int index) { selectNote(index, false); };
    QObject::connect(notes_, &QTableWidget::currentCellChanged, this,
                     [this](int row, int, int, int)
                     {
                         if (project_.staffPerformance)
                             selectStaffNote(row, true);
                         else
                             selectNote(row, true);
                     });
}

void RecognitionPreviewDialog::populateNotes()
{
    if (project_.staffPerformance)
    {
        populateStaffNotes();
        return;
    }
    const QSignalBlocker blocker(notes_);
    notes_->setRowCount(int(project_.score.notes.size()));
    for (int row = 0; row < notes_->rowCount(); ++row)
    {
        const Note &note = project_.score.notes[std::size_t(row)];
        const QStringList values{
            QString::number(row + 1),
            QString::number(note.degree),
            QString::number(note.octave),
            QString::number(double(note.durationTicks) / TicksPerQuarter, 'g', 6),
            QString::fromStdString(lyricForVerse(note, 0)),
            QString::fromStdString(lyricForVerse(note, 1)),
            QString(),
            QString(),
        };
        for (int column = 0; column < values.size(); ++column)
        {
            auto *item = new QTableWidgetItem(values[column]);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (column < 4 || column >= 6)
                item->setTextAlignment(Qt::AlignCenter);
            notes_->setItem(row, column, item);
        }
    }
}

void RecognitionPreviewDialog::populateStaffNotes()
{
    const QSignalBlocker blocker(notes_);
    notes_->setRowCount(int(project_.staffPerformance->notes.size()));
    for (int row = 0; row < notes_->rowCount(); ++row)
    {
        const auto &note = project_.staffPerformance->notes[std::size_t(row)];
        const QStringList values{QString::number(row + 1),
                                 QString::number(note.staff),
                                 QString::fromStdString(note.voice),
                                 QString::number(double(note.startTick) / TicksPerQuarter, 'g', 6),
                                 QString::number(double(note.durationTicks) / TicksPerQuarter, 'g', 6),
                                 QString::number(note.midiPitch),
                                 QString::number(note.velocity),
                                 QString()};
        for (int column = 0; column < values.size(); ++column)
        {
            auto *item = new QTableWidgetItem(values[column]);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setTextAlignment(Qt::AlignCenter);
            notes_->setItem(row, column, item);
        }
    }
}

void RecognitionPreviewDialog::selectStaffNote(int index, bool followScore)
{
    if (imageTabs_)
        imageTabs_->setCurrentIndex(0);
    if (project_.staffPerformance && index >= 0 && index < int(project_.staffPerformance->notes.size()) &&
        pageSelector_)
        pageSelector_->setCurrentIndex(project_.staffPerformance->notes[std::size_t(index)].pageIndex);
    if (!project_.staffPerformance || index < 0 || index >= notes_->rowCount())
        return;
    {
        const QSignalBlocker blocker(notes_);
        notes_->setCurrentCell(index, 0);
        notes_->selectRow(index);
    }
    if (!followScore)
        notes_->scrollToItem(notes_->item(index, 0), QAbstractItemView::PositionAtCenter);
    const auto &box = project_.staffPerformance->notes[std::size_t(index)].source;
    if (!project_.staffPerformance->notes[std::size_t(index)].hasImageAnchor || box.width <= 0 || box.height <= 0)
    {
        staffSelection_->hide();
        return;
    }
    staffSelection_->setRect(box.x - 4, box.y - 6, box.width + 8, box.height + 12);
    staffSelection_->show();
    if (followScore)
        scoreView_->ensureVisible(staffSelection_, 65, 110);
}

void RecognitionPreviewDialog::retranslateStaffNotes()
{
    const auto &performance = *project_.staffPerformance;
    std::set<std::pair<int, std::string>> voices;
    for (const auto &note : performance.notes)
        voices.emplace(note.staff, note.voice);
    summary_->setText(summary_->text() + "\n" +
                      trText("ui.preview.performance.summary")
                          .arg(performance.staffCount)
                          .arg(voices.size())
                          .arg(performance.notes.size()));
    notes_->setHorizontalHeaderLabels(
        {trText("ui.preview.column_number"), trText("ui.preview.performance.column_staff"),
         trText("ui.preview.performance.column_voice"), trText("ui.preview.performance.column_onset"),
         trText("ui.preview.column_duration"), trText("ui.preview.performance.column_pitch"),
         trText("ui.preview.performance.column_velocity"), trText("ui.preview.performance.column_ties")});
    const QSignalBlocker blocker(notes_);
    for (int row = 0; row < notes_->rowCount(); ++row)
    {
        const auto &note = performance.notes[std::size_t(row)];
        QStringList ties;
        if (note.tieStart)
            ties.append(trText("ui.preview.performance.tie_start"));
        if (note.tieStop)
            ties.append(trText("ui.preview.performance.tie_stop"));
        if (note.unresolvedSoundTie)
            ties.append(trText("ui.preview.performance.tie_unresolved"));
        notes_->item(row, 7)->setText(ties.isEmpty() ? QString(QChar(0x2014)) : ties.join(" / "));
    }
}

void RecognitionPreviewDialog::selectNote(int index, bool followScore)
{
    if (imageTabs_)
        imageTabs_->setCurrentIndex(0);
    if (index < 0 || index >= notes_->rowCount())
        return;
    if (pageSelector_)
        pageSelector_->setCurrentIndex(project_.score.notes[std::size_t(index)].pageIndex);
    {
        const QSignalBlocker blocker(notes_);
        notes_->setCurrentCell(index, 0);
        notes_->selectRow(index);
    }
    if (!followScore)
        notes_->scrollToItem(notes_->item(index, 0), QAbstractItemView::PositionAtCenter);
    scoreView_->setCurrent(index, followScore);
}

void RecognitionPreviewDialog::retranslateUi()
{
    if (!notes_)
        return;
    setWindowTitle(trText("ui.preview.window_title"));
    if (pageSelector_)
        for (int index = 0; index < pageSelector_->count(); ++index)
            pageSelector_->setItemText(index,
                                       trText("ui.pages.page_number").arg(index + 1).arg(pageSelector_->count()));
    if (imageTabs_)
    {
        imageTabs_->setTabText(0, trText("ui.local_staff.result_image"));
        const bool originalMissing =
            !project_.staffPages.empty() && project_.staffPages[std::size_t(pageIndex_)].sourceImage.isNull();
        imageTabs_->setTabText(
            1, trText(originalMissing ? "ui.pages.original_missing" : "ui.local_staff.original_image"));
    }
    source_->setText(trText("ui.preview.source").arg(sourceLabel_));
    title_->setText(trText("ui.preview.title").arg(QString::fromStdString(project_.score.title)));
    const Score &score = project_.score;
    const QString key = trText(qPrintable(QString("ui.key.%1").arg(score.tonic)));
    summary_->setText(
        trText("ui.preview.summary")
            .arg(key)
            .arg(tempoReview_ ? trText("ui.fidelity.tempo_unknown") : QString::number(score.bpm, 'g', 6))
            .arg(score.beatsPerBar)
            .arg(score.beatUnit)
            .arg(score.notes.size())
            .arg(score.repeats.size()));
    fitWidth_->setText(trText("ui.preview.fit_width"));
    warningsTitle_->setText(trText("ui.preview.warnings"));
    footer_->setText(trText("ui.preview.not_applied"));
    close_->setText(trText("ui.preview.close"));
    apply_->setText(trText("ui.preview.apply"));
    if (tempoReview_)
    {
        tempoReviewLabel_->setText(trText("ui.fidelity.tempo_review"));
        tempoConfirmed_->setText(trText("ui.fidelity.tempo_confirm"));
        tempoReview_->setSuffix(trText("ui.fidelity.tempo_unit"));
    }
    if (project_.staffPerformance)
        retranslateStaffNotes();
    else
    {
        notes_->setHorizontalHeaderLabels(
            {trText("ui.preview.column_number"), trText("ui.preview.column_degree"),
             trText("ui.preview.column_octave"), trText("ui.preview.column_duration"),
             trText("ui.preview.column_verse_a"), trText("ui.preview.column_verse_b"),
             trText("ui.preview.column_key"), trText("ui.preview.column_tie")});

        const QSignalBlocker blocker(notes_);
        for (int row = 0; row < notes_->rowCount(); ++row)
        {
            const Note &note = score.notes[std::size_t(row)];
            QString degree = QString::number(note.degree);
            if (note.degree != 0 && note.accidental != 0)
                degree.prepend(QString(std::abs(note.accidental), QChar(note.accidental > 0 ? 0x266f : 0x266d)));
            notes_->item(row, 1)->setText(note.degree == 0 ? trText("ui.preview.rest") : degree);
            const QString octave = note.octave > 0   ? trText("ui.preview.octave_up").arg(note.octave)
                                   : note.octave < 0 ? trText("ui.preview.octave_down").arg(-note.octave)
                                                     : QString::number(0);
            notes_->item(row, 2)->setText(octave);
            const QString noteKey = note.keyOverride == -1
                                        ? QString(QChar(0x2014))
                                        : trText(qPrintable(QString("ui.key.%1").arg(note.keyOverride)));
            notes_->item(row, 6)->setText(noteKey);
            notes_->item(row, 7)->setText(QString(QChar(note.tieToNext ? 0x2713 : 0x2014)));
        }
    }
    QStringList warnings;
    for (const QString &warning : project_.warnings)
        warnings.append(localizeMessage(warning));
    const int warningScrollPosition = warnings_->verticalScrollBar()->value();
    warnings_->setPlainText(warnings.isEmpty() ? trText("ui.preview.no_warnings") : warnings.join("\n\n"));
    warnings_->verticalScrollBar()->setValue(warningScrollPosition);
}

void RecognitionPreviewDialog::setPage(int index)
{
    if (index < 0 || std::size_t(index) >= project_.staffPages.size() || !scoreView_)
        return;
    pageIndex_ = index;
    const auto &page = project_.staffPages[std::size_t(index)];
    scoreView_->setScore(project_.staffImagePlayback ? page.sourceImage : page.renderedImage, project_.score,
                         index, project_.staffImagePlayback);
    staffSelection_ = nullptr;
    if (project_.staffPerformance)
    {
        scoreView_->setStaffPerformance(&*project_.staffPerformance);
        staffSelection_ = scoreView_->scene()->addRect({}, QPen(QColor("#2463EB"), 2), QColor(36, 99, 235, 55));
        staffSelection_->setZValue(11);
        staffSelection_->setData(0, QStringLiteral("staffPreviewSelection"));
        staffSelection_->hide();
    }
    if (originalView_)
        originalView_->setScore(page.sourceImage, Score{}, index);
    if (imageTabs_)
        imageTabs_->setTabText(
            1, trText(page.sourceImage.isNull() ? "ui.pages.original_missing" : "ui.local_staff.original_image"));
    scoreView_->fitWidth();
    if (originalView_)
        originalView_->fitWidth();
}

void RecognitionPreviewDialog::setApplyEnabled(bool enabled)
{
    applyAllowed_ = enabled;
    apply_->setEnabled(enabled && (!tempoConfirmed_ || tempoConfirmed_->isChecked()));
}
std::optional<Project> RecognitionPreviewDialog::projectForApply() const
{
    if (tempoConfirmed_ && !tempoConfirmed_->isChecked())
        return std::nullopt;
    Project candidate = project_;
    if (tempoReview_)
    {
        candidate.score.bpm = tempoReview_->value();
        candidate.processing.insert("tempoNeedsConfirmation", false);
        candidate.processing.insert("tempoSource", "user-confirmed");
        candidate.processing.insert("confirmedQuarterBpm", candidate.score.bpm);
    }
    return candidate;
}

void RecognitionPreviewDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}

void RecognitionPreviewDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!initialFitDone_)
    {
        initialFitDone_ = true;
        QTimer::singleShot(0, scoreView_, &ScoreView::fitWidth);
        if (originalView_)
            QTimer::singleShot(0, originalView_, &ScoreView::fitWidth);
    }
}
} // namespace singlilt
