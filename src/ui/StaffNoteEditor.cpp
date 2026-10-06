// Staff-note inspector fields, previews, and draft management.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffNoteEditor.h"

#include "application/StaffPositionMapping.h"
#include "application/StaffScoreCorrection.h"
#include "i18n/LanguageManager.h"
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace singlilt
{
namespace
{
void bindText(QObject *object, const char *key)
{
    object->setProperty("_ui_text", QByteArray(key));
    object->setProperty("text", trText(key));
}
} // namespace

StaffNoteEditor::StaffNoteEditor(QWidget *parent) : QWidget(parent)
{
    setObjectName("staffNoteEditor");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setAlignment(Qt::AlignTop);
    title_ = new QLabel(this);
    title_->setObjectName("staffNoteTitle");
    title_->setTextFormat(Qt::PlainText);
    title_->setWordWrap(true);
    layout->addWidget(title_);
    help_ = new QLabel(this);
    help_->setWordWrap(true);
    help_->setTextFormat(Qt::PlainText);
    layout->addWidget(help_);
    auto *form = new QFormLayout;
    const auto row = [this, form](const char *key, QWidget *field)
    {
        auto *label = new QLabel(this);
        bindText(label, key);
        form->addRow(label, field);
    };
    pitch_ = new QSpinBox(this);
    pitch_->setObjectName("staffNotePitch");
    pitch_->setRange(0, 127);
    row("ui.staff_correction.midi", pitch_);
    pitchName_ = new QLabel(this);
    pitchName_->setObjectName("staffNotePitchName");
    row("ui.original_playback.pitch_name", pitchName_);
    measure_ = new QComboBox(this);
    measure_->setObjectName("staffNoteMeasure");
    row("ui.staff_correction.measure_index", measure_);
    onset_ = new QLineEdit(this);
    onset_->setObjectName("staffNoteOnset");
    row("ui.staff_correction.onset", onset_);
    duration_ = new QLineEdit(this);
    duration_->setObjectName("staffNoteDuration");
    row("ui.staff_correction.duration", duration_);
    staff_ = new QSpinBox(this);
    staff_->setObjectName("staffNoteStaff");
    row("ui.staff_correction.staff", staff_);
    voice_ = new QLineEdit(this);
    voice_->setObjectName("staffNoteVoice");
    row("ui.staff_correction.voice", voice_);
    layout->addLayout(form);
    error_ = new QLabel(this);
    error_->setObjectName("staffNoteError");
    error_->setWordWrap(true);
    error_->setTextFormat(Qt::PlainText);
    layout->addWidget(error_);
    for (auto *label : {title_, help_, error_})
    {
        auto policy = label->sizePolicy();
        policy.setVerticalPolicy(QSizePolicy::Maximum);
        label->setSizePolicy(policy);
    }
    audition_ = new QPushButton(this);
    audition_->setObjectName("staffNoteAudition");
    bindText(audition_, "ui.original_playback.audition_note");
    layout->addWidget(audition_);
    auto *actions = new QHBoxLayout;
    apply_ = new QPushButton(this);
    apply_->setObjectName("staffNoteApply");
    bindText(apply_, "ui.inspector.apply");
    actions->addWidget(apply_);
    cancel_ = new QPushButton(this);
    cancel_->setObjectName("staffNoteCancel");
    bindText(cancel_, "ui.button.cancel");
    actions->addWidget(cancel_);
    delete_ = new QPushButton(this);
    delete_->setObjectName("staffNoteDelete");
    bindText(delete_, "ui.original_playback.delete_note");
    actions->addWidget(delete_);
    layout->addLayout(actions);
    layout->addStretch();

    connect(audition_, &QPushButton::clicked, this,
            [this]
            {
                try
                {
                    const auto preview = note();
                    if (auditionRequested)
                        auditionRequested(preview);
                }
                catch (const std::exception &error)
                {
                    setError(QString::fromUtf8(error.what()));
                }
            });
    connect(apply_, &QPushButton::clicked, this,
            [this]
            {
                if (applyRequested)
                    applyRequested();
            });
    connect(cancel_, &QPushButton::clicked, this,
            [this]
            {
                if (cancelRequested)
                    cancelRequested();
            });
    connect(delete_, &QPushButton::clicked, this,
            [this]
            {
                if (deleteRequested)
                    deleteRequested();
            });
    connect(measure_, &QComboBox::currentIndexChanged, this, [this](int) { updateDraftPreview(true); });
    for (auto *field : {onset_, duration_, voice_})
        connect(field, &QLineEdit::textChanged, this, [this] { updateDraftPreview(true); });
    // Spin-box valueChanged omits intermediate invalid text; raw drafts must still be tracked.
    for (auto *field : {pitch_, staff_})
    {
        connect(field, &QSpinBox::valueChanged, this, [this](int) { updateDraftPreview(true); });
        connect(field->findChild<QLineEdit *>(), &QLineEdit::textChanged, this,
                [this] { updateDraftPreview(true); });
    }
    clearNote();
}

void StaffNoteEditor::setNote(Project original, StaffPerformanceNote draft)
{
    if (!original.staffPerformance || draft.midiPitch < 0 || draft.midiPitch > 127 || draft.staff < 1 ||
        draft.staff > original.staffPerformance->staffCount)
        throw std::runtime_error(trText("ui.staff_correction.invalid_note").toStdString());
    int selected = -1;
    int choice = 0;
    for (const auto &measure : original.score.writtenMeasures)
    {
        if (measure.pageIndex != draft.pageIndex)
            continue;
        if (selected < 0 ||
            (draft.startTick >= measure.startTick && draft.startTick < measure.startTick + measure.durationTicks))
            selected = choice;
        ++choice;
    }
    if (selected < 0)
        throw std::runtime_error(trText("ui.staff_correction.invalid_position").toStdString());

    binding_ = true;
    const QSignalBlocker pitchBlock(pitch_), staffBlock(staff_), measureBlock(measure_), onsetBlock(onset_),
        durationBlock(duration_), voiceBlock(voice_);
    original_ = std::move(original);
    draft_ = std::move(draft);
    pitch_->setValue(draft_.midiPitch);
    staff_->setRange(1, original_->staffPerformance->staffCount);
    staff_->setValue(draft_.staff);
    measure_->clear();
    for (std::size_t index = 0; index < original_->score.writtenMeasures.size(); ++index)
    {
        const auto &measure = original_->score.writtenMeasures[index];
        if (measure.pageIndex == draft_.pageIndex)
            measure_->addItem(trText("ui.original_playback.measure_choice").arg(index + 1).arg(measure.number + 1),
                              int(index));
    }
    measure_->setCurrentIndex(selected);
    const auto &measure = original_->score.writtenMeasures[std::size_t(measure_->currentData().toInt())];
    onset_->setText(QString::number(
        double(std::max<std::int64_t>(0, draft_.startTick - measure.startTick)) / TicksPerQuarter, 'g', 15));
    duration_->setText(QString::number(double(draft_.durationTicks) / TicksPerQuarter, 'g', 15));
    voice_->setText(QString::fromStdString(draft_.voice));
    baselineFields_ = fields();
    bindText(title_, "ui.original_playback.note_properties");
    bindText(help_, modalControls_ ? "ui.original_playback.add_help" : "ui.original_playback.sidebar_help");
    help_->show();
    binding_ = false;
    updateDraftPreview(false);
}

void StaffNoteEditor::clearNote()
{
    binding_ = true;
    const QSignalBlocker pitchBlock(pitch_), staffBlock(staff_), measureBlock(measure_), onsetBlock(onset_),
        durationBlock(duration_), voiceBlock(voice_);
    original_.reset();
    draft_ = {};
    baselineFields_ = {};
    pitch_->setValue(0);
    staff_->setValue(staff_->minimum());
    pitch_->clear();
    staff_->clear();
    measure_->clear();
    onset_->clear();
    duration_->clear();
    voice_->clear();
    pitchName_->clear();
    error_->clear();
    bindText(title_, "ui.original_playback.no_selection");
    help_->hide();
    binding_ = false;
    refreshControls();
}

StaffPerformanceNote StaffNoteEditor::note() const
{
    if (!original_)
        throw std::runtime_error(trText("ui.original_playback.no_selection").toStdString());
    bool pitchValid = false;
    bool staffValid = false;
    const int pitch = pitch_->text().toInt(&pitchValid);
    const int staff = staff_->text().toInt(&staffValid);
    if (!pitchValid || !staffValid || pitch < 0 || pitch > 127 || staff < 1 ||
        staff > original_->staffPerformance->staffCount)
        throw std::runtime_error(trText("ui.staff_correction.invalid_note").toStdString());
    bool onsetValid = false;
    bool durationValid = false;
    const double onsetBeats = onset_->text().toDouble(&onsetValid);
    const double durationBeats = duration_->text().toDouble(&durationValid);
    if (!onsetValid || !durationValid || measure_->currentIndex() < 0)
        throw std::runtime_error(trText("ui.staff_correction.invalid_beats").toStdString());
    const auto onset = staffCorrectionTicks(onsetBeats);
    const auto duration = staffCorrectionTicks(durationBeats);
    const auto &measure = original_->score.writtenMeasures.at(std::size_t(measure_->currentData().toInt()));
    if (onset < 0 || onset >= measure.durationTicks || duration <= 0 || duration > measure.durationTicks - onset)
        throw std::runtime_error(trText("ui.staff_correction.bar_bound").toStdString());
    auto edited = draft_;
    edited.midiPitch = pitch;
    edited.startTick = measure.startTick + onset;
    edited.durationTicks = duration;
    edited.staff = staff;
    edited.voice = voice_->text().trimmed().toStdString();
    if (edited.voice.empty() || edited.voice.size() > 256)
        throw std::runtime_error(trText("ui.staff_correction.missing_fields").toStdString());
    if (edited.midiPitch != draft_.midiPitch || edited.staff != draft_.staff)
    {
        edited.staffSpelling = staffSpellingForPitch(*original_, edited, edited.midiPitch);
        const auto box = staffAnchorForPitch(*original_, edited, edited.midiPitch, edited.staffSpelling);
        if (box)
            edited.source = *box;
        else if (suggestStaffNotePosition(*original_, draft_,
                                          QPointF(draft_.source.x + draft_.source.width / 2,
                                                  draft_.source.y + draft_.source.height / 2)))
            throw std::runtime_error(trText("ui.staff_correction.invalid_position").toStdString());
    }
    return edited;
}

std::array<QString, 6> StaffNoteEditor::fields() const
{
    return {pitch_->text(), QString::number(measure_->currentIndex() < 0 ? -1 : measure_->currentData().toInt()),
            onset_->text(), duration_->text(),
            staff_->text(), voice_->text()};
}

bool StaffNoteEditor::hasDraft() const
{
    return original_ && fields() != baselineFields_;
}

void StaffNoteEditor::updateDraftPreview(bool notify)
{
    if (binding_ || !original_)
        return;
    bool pitchValid = false;
    const int pitch = pitch_->text().toInt(&pitchValid);
    if (pitchValid && pitch >= 0 && pitch <= 127)
    {
        static const char *names[]{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        pitchName_->setText(QString::fromLatin1(names[pitch % 12]) + QString::number(pitch / 12 - 1));
    }
    else
        pitchName_->clear();
    try
    {
        const auto preview = note();
        error_->clear();
        if (notify && draftPreviewChanged)
            draftPreviewChanged(preview);
    }
    catch (const std::exception &error)
    {
        setError(QString::fromUtf8(error.what()));
    }
    refreshControls();
    if (notify && draftChanged)
        draftChanged();
}

void StaffNoteEditor::refreshControls()
{
    const bool enabled = editingEnabled_ && original_.has_value();
    for (auto *field : std::initializer_list<QWidget *>{pitch_, staff_, measure_, onset_, duration_, voice_})
        field->setEnabled(enabled);
    audition_->setEnabled(enabled);
    apply_->setEnabled(enabled && hasDraft());
    cancel_->setEnabled(enabled && hasDraft());
    delete_->setEnabled(enabled && !hasDraft());
}

void StaffNoteEditor::setEditingEnabled(bool enabled)
{
    editingEnabled_ = enabled;
    refreshControls();
}

void StaffNoteEditor::discardDraft()
{
    if (original_)
        setNote(*original_, draft_);
}

void StaffNoteEditor::setError(const QString &message)
{
    error_->setText(message);
}

void StaffNoteEditor::requireOnsetInput()
{
    onset_->clear();
}

void StaffNoteEditor::setModalControls(bool modal)
{
    modalControls_ = modal;
    layout()->setContentsMargins(modal ? 0 : 12, 0, modal ? 0 : 12, 0);
    title_->setVisible(!modal);
    apply_->setVisible(!modal);
    cancel_->setVisible(!modal);
    delete_->setVisible(!modal);
    if (original_)
        bindText(help_, modal ? "ui.original_playback.add_help" : "ui.original_playback.sidebar_help");
}
} // namespace singlilt
