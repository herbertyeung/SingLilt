// Arrangement review, chord editing, and preview controls.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AccompanimentPanel.h"
#include "i18n/LanguageManager.h"
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>

namespace singlilt
{
namespace
{
void bind(QObject *object, const char *key, const char *property = "text")
{
    object->setProperty((QByteArray("_ui_") + property).constData(), QByteArray(key));
    object->setProperty(property, trText(key));
}
QLabel *textLabel(const char *key)
{
    auto *label = new QLabel;
    bind(label, key);
    return label;
}
void addItem(QComboBox *combo, const char *key, int value)
{
    combo->addItem(trText(key), value);
    combo->setItemData(combo->count() - 1, QByteArray(key), Qt::UserRole + 1);
}
QPushButton *action(const char *key, const char *name, QBoxLayout *layout)
{
    auto *button = new QPushButton;
    button->setObjectName(name);
    bind(button, key);
    layout->addWidget(button);
    return button;
}
QString chordText(const ChordSpan &chord)
{
    if (chord.quality == ChordQuality::None)
        return trText("ui.accompaniment.none");
    const char *names[] = {"C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"};
    const QString suffix = chord.quality == ChordQuality::Minor        ? "m"
                           : chord.quality == ChordQuality::Diminished ? "dim"
                                                                       : "";
    return QString::fromUtf8(names[chord.rootPitchClass]) + suffix;
}
const char *patternKey(AccompanimentPattern pattern)
{
    return pattern == AccompanimentPattern::Arpeggio ? "ui.accompaniment.arpeggio"
           : pattern == AccompanimentPattern::Sparse ? "ui.whole.sparse_pattern"
                                                     : "ui.accompaniment.block";
}
bool hasErrors(const std::vector<Diagnostic> &diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic &diagnostic)
                       { return diagnostic.severity == DiagnosticSeverity::Error; });
}
bool replaceDraft(QWidget *parent)
{
    QMessageBox question(QMessageBox::Question, trText("ui.whole.replace_title"), trText("ui.whole.replace_draft"),
                         QMessageBox::Yes | QMessageBox::No, parent);
    question.setObjectName("accompanimentReplaceDraftConfirmation");
    question.setDefaultButton(QMessageBox::No);
    return question.exec() == QMessageBox::Yes;
}
} // namespace

AccompanimentPanel::AccompanimentPanel(const Score &score, AccompanimentArrangement candidate, QWidget *parent)
    : AccompanimentPanel(score, std::move(candidate), std::nullopt, parent)
{
}
AccompanimentPanel::AccompanimentPanel(const Score &score, AccompanimentArrangement candidate,
                                       std::optional<AccompanimentArrangement> confirmed, QWidget *parent)
    : QDialog(parent), score_(score), candidate_(std::move(candidate)),
      confirmedArrangement_(std::move(confirmed)), measures_(sourceMeasureRanges(score)),
      currentFingerprint_(accompanimentFingerprint(score))
{
    candidates_ = generateAccompanimentCandidates(score_, candidate_.settings);
    if (!candidates_.empty())
        candidate_ = candidates_.front().arrangement;
    if (confirmedArrangement_ && confirmedArrangement_->userAuthored)
        manualDraft_ = confirmedArrangement_;
    setObjectName("accompanimentPreview");
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    resize(880, 800);
    bind(this, "ui.whole.title", "windowTitle");
    auto *layout = new QVBoxLayout(this);
    auto *hint = textLabel("ui.whole.hint");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    wholeSong_ = new QListWidget;
    wholeSong_->setObjectName("accompanimentWholeSongCandidates");
    wholeSong_->setMaximumHeight(168);
    layout->addWidget(wholeSong_);
    auto *copies = new QHBoxLayout;
    auto *copy = action("ui.whole.copy", "accompanimentCopyCandidate", copies);
    auto *restore = action("ui.whole.restore", "accompanimentRestoreConfirmed", copies);
    auto *blank = action("ui.whole.blank", "accompanimentBlankDraft", copies);
    copies->addWidget(textLabel("ui.whole.name"));
    draftName_ = new QLineEdit;
    draftName_->setObjectName("accompanimentDraftName");
    draftName_->setMaxLength(128);
    copies->addWidget(draftName_, 1);
    layout->addLayout(copies);
    auto *settings = new QHBoxLayout;
    pattern_ = new QComboBox;
    pattern_->setObjectName("candidatePattern");
    addItem(pattern_, "ui.accompaniment.block", int(AccompanimentPattern::BlockChords));
    addItem(pattern_, "ui.accompaniment.arpeggio", int(AccompanimentPattern::Arpeggio));
    addItem(pattern_, "ui.whole.sparse_pattern", int(AccompanimentPattern::Sparse));
    mode_ = new QComboBox;
    mode_->setObjectName("accompanimentMode");
    addItem(mode_, "ui.accompaniment.auto", int(HarmonicMode::Automatic));
    addItem(mode_, "ui.accompaniment.major_mode", int(HarmonicMode::Major));
    addItem(mode_, "ui.accompaniment.minor_mode", int(HarmonicMode::Minor));
    tonic_ = new QComboBox;
    tonic_->setObjectName("accompanimentTonic");
    addItem(tonic_, "ui.accompaniment.source_key", -1);
    for (int i = 0; i < 12; ++i)
        addItem(tonic_, qPrintable(QString("ui.key.%1").arg(i)), i);
    mode_->setCurrentIndex(mode_->findData(int(candidate_.settings.mode)));
    tonic_->setCurrentIndex(tonic_->findData(candidate_.settings.harmonicTonic));
    settings->addWidget(textLabel("ui.accompaniment.pattern"));
    settings->addWidget(pattern_);
    settings->addWidget(mode_);
    settings->addWidget(tonic_);
    auto *generate = action("ui.whole.regenerate", "accompanimentRegenerate", settings);
    layout->addLayout(settings);
    auto *advancedToggle = new QPushButton;
    advancedToggle->setObjectName("accompanimentAdvancedToggle");
    advancedToggle->setCheckable(true);
    bind(advancedToggle, "ui.whole.advanced");
    layout->addWidget(advancedToggle);
    sections_ = new QListWidget;
    sections_->setObjectName("accompanimentSections");
    layout->addWidget(sections_, 1);
    advanced_ = new QWidget;
    advanced_->setObjectName("accompanimentAdvancedEditor");
    auto *advancedLayout = new QVBoxLayout(advanced_);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    chords_ = new QTableWidget(0, 4);
    chords_->setObjectName("accompanimentChords");
    chords_->setProperty("translationHeaders",
                         QStringList{"ui.accompaniment.interval", "ui.accompaniment.chord",
                                     "ui.accompaniment.edit_status", "ui.whole.section_pattern"});
    chords_->setSelectionBehavior(QAbstractItemView::SelectRows);
    chords_->setSelectionMode(QAbstractItemView::SingleSelection);
    chords_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    chords_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    advancedLayout->addWidget(chords_, 1);
    auto *edit = new QHBoxLayout;
    alternatives_ = new QComboBox;
    alternatives_->setObjectName("accompanimentCandidate");
    edit->addWidget(textLabel("ui.accompaniment.alternative"));
    edit->addWidget(alternatives_);
    root_ = new QComboBox;
    root_->setObjectName("accompanimentRoot");
    for (int i = 0; i < 12; ++i)
        addItem(root_, qPrintable(QString("ui.key.%1").arg(i)), i);
    edit->addWidget(textLabel("ui.accompaniment.root"));
    edit->addWidget(root_);
    quality_ = new QComboBox;
    quality_->setObjectName("accompanimentQuality");
    addItem(quality_, "ui.accompaniment.major", int(ChordQuality::Major));
    addItem(quality_, "ui.accompaniment.minor", int(ChordQuality::Minor));
    addItem(quality_, "ui.accompaniment.diminished", int(ChordQuality::Diminished));
    addItem(quality_, "ui.accompaniment.none", int(ChordQuality::None));
    edit->addWidget(quality_);
    inversion_ = new QSpinBox;
    inversion_->setObjectName("accompanimentInversion");
    inversion_->setRange(0, 2);
    edit->addWidget(textLabel("ui.accompaniment.inversion"));
    edit->addWidget(inversion_);
    advancedLayout->addLayout(edit);
    auto *interval = new QHBoxLayout;
    startTick_ = new QSpinBox;
    startTick_->setObjectName("accompanimentStartTick");
    endTick_ = new QSpinBox;
    endTick_->setObjectName("accompanimentEndTick");
    std::int64_t duration = 0;
    for (const auto &note : score_.notes)
        duration += note.durationTicks;
    startTick_->setRange(0, int(std::min<std::int64_t>(duration, INT_MAX)));
    endTick_->setRange(0, int(std::min<std::int64_t>(duration, INT_MAX)));
    interval->addWidget(textLabel("ui.whole.start_tick"));
    interval->addWidget(startTick_);
    interval->addWidget(textLabel("ui.whole.end_tick"));
    interval->addWidget(endTick_);
    sectionPattern_ = new QComboBox;
    sectionPattern_->setObjectName("accompanimentSectionPattern");
    addItem(sectionPattern_, "ui.whole.inherit_pattern", -1);
    addItem(sectionPattern_, "ui.accompaniment.block", int(AccompanimentPattern::BlockChords));
    addItem(sectionPattern_, "ui.accompaniment.arpeggio", int(AccompanimentPattern::Arpeggio));
    addItem(sectionPattern_, "ui.whole.sparse_pattern", int(AccompanimentPattern::Sparse));
    interval->addWidget(sectionPattern_);
    auto *apply = action("ui.whole.apply_interval", "accompanimentApplyInterval", interval);
    advancedLayout->addLayout(interval);
    auto *intervalActions = new QHBoxLayout;
    auto *split = action("ui.whole.split", "accompanimentSplitSpan", intervalActions);
    auto *merge = action("ui.whole.merge", "accompanimentMergeSpan", intervalActions);
    bind(merge, "ui.whole.merge_tip", "toolTip");
    auto *add = action("ui.whole.add", "accompanimentAddSpan", intervalActions);
    auto *remove = action("ui.whole.delete", "accompanimentDeleteSpan", intervalActions);
    advancedLayout->addLayout(intervalActions);
    layout->addWidget(advanced_, 1);
    advanced_->hide();
    status_ = new QLabel;
    status_->setObjectName("accompanimentStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *actions = new QHBoxLayout;
    audition_ = action("ui.accompaniment.audition", "accompanimentAudition", actions);
    actions->addStretch();
    confirm_ = action("ui.accompaniment.confirm", "accompanimentConfirm", actions);
    auto *cancel = action("ui.button.cancel", "accompanimentCancel", actions);
    layout->addLayout(actions);
    connect(wholeSong_, &QListWidget::currentRowChanged, this, [this] { chooseWholeSong(); });
    connect(copy, &QPushButton::clicked, this, [this] { copyCandidate(); });
    connect(restore, &QPushButton::clicked, this, [this] { restoreConfirmed(); });
    connect(blank, &QPushButton::clicked, this, [this] { createBlankDraft(); });
    connect(draftName_, &QLineEdit::textEdited, this,
            [this](const QString &name)
            {
                if (selectedCandidate_ == int(candidates_.size()))
                {
                    candidate_.name = name.toStdString();
                    saveManualDraft();
                    refreshCandidates();
                }
            });
    connect(advancedToggle, &QPushButton::toggled, this,
            [this](bool expanded)
            {
                advanced_->setVisible(expanded);
                sections_->setVisible(!expanded);
            });
    connect(sections_, &QListWidget::currentRowChanged, this,
            [this](int row)
            {
                QSignalBlocker blocker(chords_);
                if (row >= 0)
                    chords_->selectRow(row);
                selectChord();
            });
    connect(chords_, &QTableWidget::itemSelectionChanged, this,
            [this]
            {
                QSignalBlocker blocker(sections_);
                sections_->setCurrentRow(chords_->currentRow());
                selectChord();
            });
    connect(root_, &QComboBox::currentIndexChanged, this, [this] { editChord(); });
    connect(quality_, &QComboBox::currentIndexChanged, this, [this] { editChord(); });
    connect(inversion_, &QSpinBox::valueChanged, this, [this] { editChord(); });
    connect(alternatives_, &QComboBox::currentIndexChanged, this, [this] { selectAlternative(); });
    connect(sectionPattern_, &QComboBox::currentIndexChanged, this, [this] { editChord(); });
    connect(pattern_, &QComboBox::currentIndexChanged, this,
            [this]
            {
                if (updating_ || stale_ || selectedCandidate_ != int(candidates_.size()))
                    return;
                candidate_.settings.pattern = AccompanimentPattern(pattern_->currentData().toInt());
                saveManualDraft();
                refreshTable();
                refreshAudition();
            });
    connect(apply, &QPushButton::clicked, this, [this] { applyInterval(); });
    connect(split, &QPushButton::clicked, this, [this] { splitInterval(); });
    connect(merge, &QPushButton::clicked, this, [this] { mergeInterval(); });
    connect(add, &QPushButton::clicked, this, [this] { addInterval(); });
    connect(remove, &QPushButton::clicked, this, [this] { deleteInterval(); });
    connect(generate, &QPushButton::clicked, this, [this] { regenerate(); });
    connect(audition_, &QPushButton::clicked, this,
            [this]
            {
                updateValidity();
                if (!stale_ && !hasErrors(validateAccompaniment(score_, candidate_)) && auditionRequested)
                    auditioning_ = auditionRequested(candidate_);
            });
    connect(confirm_, &QPushButton::clicked, this,
            [this]
            {
                updateValidity();
                if (!stale_ && !hasErrors(validateAccompaniment(score_, candidate_)) && confirmationRequested &&
                    confirmationRequested(candidate_))
                {
                    confirmed_ = true;
                    accept();
                }
            });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(this, &QDialog::finished, this,
            [this]
            {
                if (!confirmed_ && cancelled)
                    cancelled();
            });
    refreshCandidates();
    refreshTable();
    retranslateUi();
}
const AccompanimentArrangement &AccompanimentPanel::candidate() const
{
    return candidate_;
}
void AccompanimentPanel::setSourceFingerprint(const std::string &fingerprint)
{
    currentFingerprint_ = fingerprint;
    stale_ = currentFingerprint_ != candidate_.melodyFingerprint;
    updateValidity();
}
void AccompanimentPanel::refreshCandidates()
{
    QSignalBlocker blocker(wholeSong_);
    wholeSong_->clear();
    for (const auto &candidate : candidates_)
    {
        const QString label = trText(candidate.labelKey.c_str());
        const QString descriptionKey =
            QString("ui.whole.description.%1").arg(QString::fromStdString(candidate.id));
        const QString details = trText(qPrintable(descriptionKey));
        auto *item = new QListWidgetItem(label + "\n" + details, wholeSong_);
        item->setData(Qt::UserRole, QString::fromStdString(candidate.id));
        item->setSizeHint(QSize(0, 38));
    }
    const QString name = manualDraft_ && !manualDraft_->name.empty() ? QString::fromStdString(manualDraft_->name)
                                                                     : trText("ui.whole.my_arrangement");
    auto *manual = new QListWidgetItem(trText("ui.whole.manual_entry").arg(name), wholeSong_);
    manual->setData(Qt::UserRole, "manual");
    manual->setSizeHint(QSize(0, 38));
    wholeSong_->setCurrentRow(selectedCandidate_);
}
void AccompanimentPanel::chooseWholeSong()
{
    if (updating_ || wholeSong_->currentRow() < 0)
        return;
    saveManualDraft();
    selectedCandidate_ = wholeSong_->currentRow();
    if (selectedCandidate_ < int(candidates_.size()))
        candidate_ = candidates_[size_t(selectedCandidate_)].arrangement;
    else if (manualDraft_)
        candidate_ = *manualDraft_;
    else
    {
        createBlankDraft();
        return;
    }
    stale_ = candidate_.melodyFingerprint != currentFingerprint_;
    refreshTable();
    refreshAudition();
}
void AccompanimentPanel::selectManualDraft(AccompanimentArrangement arrangement)
{
    arrangement.userAuthored = true;
    if (arrangement.name.empty())
        arrangement.name = trText("ui.whole.my_arrangement").toStdString();
    manualDraft_ = arrangement;
    candidate_ = std::move(arrangement);
    selectedCandidate_ = int(candidates_.size());
    stale_ = candidate_.melodyFingerprint != currentFingerprint_;
    refreshCandidates();
    refreshTable();
    refreshAudition();
}
void AccompanimentPanel::copyCandidate()
{
    if (stale_ || selectedCandidate_ == int(candidates_.size()) || (manualDraft_ && !replaceDraft(this)))
        return;
    auto copied = candidate_;
    copied.name = trText("ui.whole.my_arrangement").toStdString();
    selectManualDraft(std::move(copied));
}
void AccompanimentPanel::restoreConfirmed()
{
    if (!confirmedArrangement_ || (manualDraft_ && !replaceDraft(this)))
        return;
    selectManualDraft(*confirmedArrangement_);
}
void AccompanimentPanel::createBlankDraft()
{
    if (manualDraft_ && !replaceDraft(this))
        return;
    AccompanimentArrangement blank;
    blank.settings = candidate_.settings;
    blank.melodyFingerprint = accompanimentFingerprint(score_);
    blank.name = trText("ui.whole.my_arrangement").toStdString();
    blank.userAuthored = true;
    for (const auto &measure : measures_)
        blank.chords.push_back({measure.startTick, measure.endTick, 0, ChordQuality::None, 0, true});
    selectManualDraft(std::move(blank));
}
void AccompanimentPanel::saveManualDraft()
{
    if (selectedCandidate_ == int(candidates_.size()))
        manualDraft_ = candidate_;
}
void AccompanimentPanel::refreshAudition()
{
    if (auditioning_ && !stale_ && auditionRequested && !hasErrors(validateAccompaniment(score_, candidate_)))
        auditionRequested(candidate_);
}
QString AccompanimentPanel::musicalInterval(const ChordSpan &span) const
{
    const auto first =
        std::find_if(measures_.begin(), measures_.end(), [&span](const SourceMeasureRange &measure)
                     { return span.startTick >= measure.startTick && span.startTick < measure.endTick; });
    if (first == measures_.end())
        return trText("ui.accompaniment.tick_range").arg(span.startTick).arg(span.endTick);
    const double ticksPerBeat = TicksPerQuarter * 4.0 / score_.beatUnit;
    const double startBeat = 1.0 + (span.startTick - first->startTick) / ticksPerBeat;
    const double length = (span.endTick - span.startTick) / ticksPerBeat;
    const QString measure =
        first->pickup ? trText("ui.whole.pickup")
                      : trText("ui.whole.measure")
                            .arg(first->measure + 1 - (!measures_.empty() && measures_.front().pickup ? 1 : 0));
    return trText("ui.whole.musical_interval")
        .arg(measure, QString::number(startBeat, 'g', 4), QString::number(length, 'g', 4));
}
void AccompanimentPanel::refreshTable()
{
    const int selected = std::max(0, chords_->currentRow());
    const QSignalBlocker tableBlock(chords_), sectionBlock(sections_), patternBlock(pattern_),
        nameBlock(draftName_);
    chords_->setRowCount(int(candidate_.chords.size()));
    sections_->clear();
    for (int row = 0; row < chords_->rowCount(); ++row)
    {
        const auto &chord = candidate_.chords[size_t(row)];
        const auto pattern = chord.patternOverride.value_or(candidate_.settings.pattern);
        const QString text =
            chord.quality == ChordQuality::None ? trText("ui.accompaniment.none") : trText(patternKey(pattern));
        sections_->addItem(musicalInterval(chord) + " — " + text);
        chords_->setItem(
            row, 0,
            new QTableWidgetItem(trText("ui.accompaniment.tick_range").arg(chord.startTick).arg(chord.endTick)));
        chords_->setItem(row, 1,
                         new QTableWidgetItem(chordText(chord) +
                                              trText("ui.accompaniment.inversion_suffix").arg(chord.inversion)));
        chords_->setItem(row, 2,
                         new QTableWidgetItem(
                             trText(chord.userEdited ? "ui.accompaniment.edited" : "ui.accompaniment.suggested")));
        chords_->setItem(row, 3,
                         new QTableWidgetItem(chord.patternOverride ? trText(patternKey(pattern))
                                                                    : trText("ui.whole.inherit_pattern")));
    }
    if (chords_->rowCount())
    {
        const int row = std::min(selected, chords_->rowCount() - 1);
        chords_->selectRow(row);
        sections_->setCurrentRow(row);
    }
    pattern_->setCurrentIndex(pattern_->findData(int(candidate_.settings.pattern)));
    draftName_->setText(QString::fromStdString(candidate_.name));
    selectChord();
    updateValidity();
}
void AccompanimentPanel::selectChord()
{
    updating_ = true;
    const int row = chords_->currentRow();
    const bool valid = row >= 0 && row < int(candidate_.chords.size());
    const bool editable = valid && !stale_ && selectedCandidate_ == int(candidates_.size());
    QWidget *editors[] = {root_, quality_, inversion_, alternatives_, startTick_, endTick_, sectionPattern_};
    for (auto *widget : editors)
        widget->setEnabled(editable);
    if (valid)
    {
        const auto &chord = candidate_.chords[size_t(row)];
        root_->setCurrentIndex(root_->findData(chord.rootPitchClass));
        quality_->setCurrentIndex(quality_->findData(int(chord.quality)));
        inversion_->setValue(chord.inversion);
        startTick_->setValue(int(chord.startTick));
        endTick_->setValue(int(chord.endTick));
        sectionPattern_->setCurrentIndex(
            sectionPattern_->findData(chord.patternOverride ? int(*chord.patternOverride) : -1));
        alternatives_->clear();
        addItem(alternatives_, "ui.accompaniment.custom", -1);
        const int majorOffsets[] = {0, 2, 4, 5, 7, 9, 11};
        const int minorOffsets[] = {0, 2, 3, 5, 7, 8, 10};
        const ChordQuality majorQualities[] = {ChordQuality::Major,     ChordQuality::Minor, ChordQuality::Minor,
                                               ChordQuality::Major,     ChordQuality::Major, ChordQuality::Minor,
                                               ChordQuality::Diminished};
        const ChordQuality minorQualities[] = {ChordQuality::Minor, ChordQuality::Diminished, ChordQuality::Major,
                                               ChordQuality::Minor, ChordQuality::Minor,      ChordQuality::Major,
                                               ChordQuality::Major};
        const auto tonality = resolveHarmonicTonality(score_, candidate_.settings, chord.startTick);
        const bool minor = tonality.mode == HarmonicMode::Minor;
        for (int i = 0; i < 7; ++i)
        {
            const int root = (tonality.tonic + (minor ? minorOffsets[i] : majorOffsets[i])) % 12;
            const auto quality = minor ? minorQualities[i] : majorQualities[i];
            ChordSpan alternative = chord;
            alternative.rootPitchClass = root;
            alternative.quality = quality;
            alternatives_->addItem(chordText(alternative), root * 4 + int(quality));
        }
        addItem(alternatives_, "ui.accompaniment.none", 4 * 12 + int(ChordQuality::None));
        alternatives_->setCurrentIndex(0);
    }
    updating_ = false;
}
void AccompanimentPanel::editChord()
{
    const int row = chords_->currentRow();
    if (updating_ || stale_ || selectedCandidate_ != int(candidates_.size()) || row < 0 ||
        row >= int(candidate_.chords.size()))
        return;
    auto edited = candidate_;
    auto &chord = edited.chords[size_t(row)];
    chord.rootPitchClass = root_->currentData().toInt();
    chord.quality = ChordQuality(quality_->currentData().toInt());
    chord.inversion = chord.quality == ChordQuality::None ? 0 : inversion_->value();
    chord.userEdited = true;
    const int pattern = sectionPattern_->currentData().toInt();
    chord.patternOverride =
        pattern < 0 ? std::nullopt : std::optional<AccompanimentPattern>(AccompanimentPattern(pattern));
    applyManualArrangement(std::move(edited), row);
}
void AccompanimentPanel::selectAlternative()
{
    if (updating_ || stale_ || alternatives_->currentData().toInt() < 0)
        return;
    const int choice = alternatives_->currentData().toInt();
    updating_ = true;
    root_->setCurrentIndex(root_->findData(choice / 4 % 12));
    quality_->setCurrentIndex(quality_->findData(choice % 4));
    inversion_->setValue(0);
    updating_ = false;
    editChord();
}
bool AccompanimentPanel::applyManualArrangement(AccompanimentArrangement arrangement, int selectedRow)
{
    if (stale_ || selectedCandidate_ != int(candidates_.size()))
        return false;
    const auto diagnostics = validateAccompaniment(score_, arrangement);
    if (hasErrors(diagnostics))
    {
        QStringList messages;
        for (const auto &diagnostic : diagnostics)
            if (diagnostic.severity == DiagnosticSeverity::Error)
                messages.append(localizeMessage(QString::fromStdString(diagnostic.message)));
        messages.removeDuplicates();
        status_->setText(messages.join('\n'));
        return false;
    }
    arrangement.userAuthored = true;
    candidate_ = std::move(arrangement);
    saveManualDraft();
    refreshTable();
    if (selectedRow >= 0 && selectedRow < chords_->rowCount())
        chords_->selectRow(selectedRow);
    refreshAudition();
    return true;
}
void AccompanimentPanel::applyInterval()
{
    const int row = chords_->currentRow();
    if (row < 0 || row >= int(candidate_.chords.size()))
        return;
    auto edited = candidate_;
    edited.chords[size_t(row)].startTick = startTick_->value();
    edited.chords[size_t(row)].endTick = endTick_->value();
    edited.chords[size_t(row)].userEdited = true;
    applyManualArrangement(std::move(edited), row);
}
void AccompanimentPanel::splitInterval()
{
    const int row = chords_->currentRow();
    if (row < 0 || row >= int(candidate_.chords.size()))
        return;
    auto edited = candidate_;
    auto first = edited.chords[size_t(row)];
    if (first.endTick - first.startTick < 2)
        return;
    auto second = first;
    first.endTick = first.startTick + (first.endTick - first.startTick) / 2;
    second.startTick = first.endTick;
    first.userEdited = second.userEdited = true;
    edited.chords[size_t(row)] = first;
    edited.chords.insert(edited.chords.begin() + row + 1, second);
    applyManualArrangement(std::move(edited), row);
}
void AccompanimentPanel::mergeInterval()
{
    const int row = chords_->currentRow();
    if (row < 0 || row + 1 >= int(candidate_.chords.size()))
        return;
    auto edited = candidate_;
    auto &first = edited.chords[size_t(row)];
    if (first.endTick != edited.chords[size_t(row + 1)].startTick)
    {
        status_->setText(trText("ui.whole.merge_adjacent"));
        return;
    }
    first.endTick = edited.chords[size_t(row + 1)].endTick;
    first.userEdited = true;
    edited.chords.erase(edited.chords.begin() + row + 1);
    applyManualArrangement(std::move(edited), row);
}
void AccompanimentPanel::addInterval()
{
    auto edited = candidate_;
    std::int64_t sourceDuration = 0;
    for (const auto &note : score_.notes)
        sourceDuration += note.durationTicks;
    std::int64_t start = 0;
    size_t index = 0;
    while (index < edited.chords.size() && edited.chords[index].startTick == start)
    {
        start = edited.chords[index].endTick;
        ++index;
    }
    const auto end = index < edited.chords.size() ? edited.chords[index].startTick : sourceDuration;
    if (end <= start)
    {
        status_->setText(trText("ui.whole.no_gap"));
        return;
    }
    edited.chords.insert(edited.chords.begin() + std::ptrdiff_t(index),
                         {start, end, 0, ChordQuality::None, 0, true});
    applyManualArrangement(std::move(edited), int(index));
}
void AccompanimentPanel::deleteInterval()
{
    const int row = chords_->currentRow();
    if (row < 0 || row >= int(candidate_.chords.size()))
        return;
    auto edited = candidate_;
    edited.chords.erase(edited.chords.begin() + row);
    applyManualArrangement(std::move(edited), std::max(0, row - 1));
}
void AccompanimentPanel::regenerate()
{
    if (currentFingerprint_ != accompanimentFingerprint(score_))
        return;
    saveManualDraft();
    auto settings = candidate_.settings;
    settings.mode = HarmonicMode(mode_->currentData().toInt());
    settings.harmonicTonic = tonic_->currentData().toInt();
    auto generated = generateAccompanimentCandidates(score_, settings);
    if (generated.empty())
    {
        status_->setText(trText("ui.whole.generation_failed"));
        return;
    }
    const bool manual = selectedCandidate_ == int(candidates_.size());
    candidates_ = std::move(generated);
    if (manual)
        selectedCandidate_ = int(candidates_.size());
    else
    {
        selectedCandidate_ = std::min(selectedCandidate_, int(candidates_.size()) - 1);
        candidate_ = candidates_[size_t(selectedCandidate_)].arrangement;
    }
    refreshCandidates();
    refreshTable();
    refreshAudition();
}
void AccompanimentPanel::updateValidity()
{
    const auto validation = validateAccompaniment(score_, candidate_);
    const bool valid = !stale_ && candidate_.valid() && !hasErrors(validation);
    const bool manual = selectedCandidate_ == int(candidates_.size());
    audition_->setEnabled(valid);
    confirm_->setEnabled(valid);
    pattern_->setEnabled(manual && !stale_);
    draftName_->setEnabled(manual && !stale_);
    mode_->setEnabled(currentFingerprint_ == accompanimentFingerprint(score_));
    tonic_->setEnabled(mode_->isEnabled());
    if (auto *generate = findChild<QPushButton *>("accompanimentRegenerate"))
        generate->setEnabled(mode_->isEnabled());
    if (auto *restore = findChild<QPushButton *>("accompanimentRestoreConfirmed"))
        restore->setEnabled(confirmedArrangement_.has_value());
    if (auto *copy = findChild<QPushButton *>("accompanimentCopyCandidate"))
        copy->setEnabled(!manual && !stale_);
    if (auto *blank = findChild<QPushButton *>("accompanimentBlankDraft"))
        blank->setEnabled(currentFingerprint_ == accompanimentFingerprint(score_));
    for (const char *name : {"accompanimentApplyInterval", "accompanimentSplitSpan", "accompanimentMergeSpan",
                             "accompanimentDeleteSpan"})
        if (auto *button = findChild<QPushButton *>(name))
            button->setEnabled(manual && !stale_ && chords_->currentRow() >= 0);
    if (auto *add = findChild<QPushButton *>("accompanimentAddSpan"))
        add->setEnabled(manual && !stale_);
    selectChord();
    QStringList diagnostics;
    for (const auto &diagnostic : candidate_.diagnostics)
        diagnostics.append(localizeMessage(QString::fromStdString(diagnostic.message)));
    for (const auto &diagnostic : validation)
        if (diagnostic.severity == DiagnosticSeverity::Error)
            diagnostics.append(localizeMessage(QString::fromStdString(diagnostic.message)));
    if (stale_)
        diagnostics.prepend(trText("ui.accompaniment.stale_candidate"));
    diagnostics.removeDuplicates();
    status_->setText(diagnostics.join('\n'));
}
void AccompanimentPanel::retranslateUi()
{
    auto objects = findChildren<QObject *>();
    objects.prepend(this);
    for (auto *object : objects)
    {
        QSignalBlocker blocker(object);
        for (const auto &property : object->dynamicPropertyNames())
            if (property.startsWith("_ui_"))
                object->setProperty(property.mid(4).constData(),
                                    trText(object->property(property.constData()).toByteArray().constData()));
        if (auto *combo = qobject_cast<QComboBox *>(object))
            for (int i = 0; i < combo->count(); ++i)
            {
                const auto key = combo->itemData(i, Qt::UserRole + 1).toByteArray();
                if (!key.isEmpty())
                    combo->setItemText(i, trText(key.constData()));
            }
    }
    QStringList headers;
    for (const auto &key : chords_->property("translationHeaders").toStringList())
        headers.append(trText(qPrintable(key)));
    chords_->setHorizontalHeaderLabels(headers);
    refreshCandidates();
    refreshTable();
}
} // namespace singlilt
