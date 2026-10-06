// Staff-note pitch, timing, and source-position entry.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffNoteDialog.h"

#include "StaffNoteEditor.h"
#include "application/StaffNoteEditing.h"
#include "i18n/LanguageManager.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <stdexcept>
#include <utility>

namespace singlilt
{
StaffNoteDialog::StaffNoteDialog(Project project, StaffPerformanceNote draft, QWidget *parent, int editingIndex)
    : QDialog(parent), original_(std::move(project)), draft_(std::move(draft)), editingIndex_(editingIndex)
{
    setObjectName("staffNoteAddDialog");
    setWindowTitle(
        trText(editingIndex_ < 0 ? "ui.original_playback.add_note" : "ui.original_playback.note_properties"));
    resize(520, 380);
    auto *layout = new QVBoxLayout(this);
    timingHint_ = new QLabel(this);
    timingHint_->setObjectName("staffNoteTimingHint");
    timingHint_->setWordWrap(true);
    timingHint_->setTextFormat(Qt::PlainText);
    timingHint_->hide();
    layout->addWidget(timingHint_);
    editor_ = new StaffNoteEditor(this);
    editor_->setModalControls(true);
    editor_->setNote(original_, draft_);
    editor_->draftPreviewChanged = [this](const StaffPerformanceNote &note)
    {
        if (draftPreviewChanged)
            draftPreviewChanged(note);
    };
    editor_->auditionRequested = [this](const StaffPerformanceNote &note)
    {
        if (auditionRequested)
            auditionRequested(note);
    };
    layout->addWidget(editor_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setObjectName("staffNoteAddApply");
    buttons->button(QDialogButtonBox::Ok)
        ->setText(trText(editingIndex_ < 0 ? "ui.original_playback.add_note" : "ui.staff_correction.apply"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void StaffNoteDialog::setTimingSuggestion(const QString &message, bool available)
{
    timingHint_->setText(message);
    timingHint_->setVisible(!message.isEmpty());
    if (!available)
        editor_->requireOnsetInput();
}

void StaffNoteDialog::accept()
{
    try
    {
        draft_ = editor_->note();
        corrected_ = editingIndex_ < 0 ? addedStaffNote(original_, draft_)
                                       : updatedStaffNote(original_, editingIndex_, draft_);
        QDialog::accept();
    }
    catch (const std::exception &error)
    {
        corrected_.reset();
        editor_->setError(QString::fromUtf8(error.what()));
    }
}

const std::optional<Project> &StaffNoteDialog::correctedProject() const
{
    return corrected_;
}

const StaffPerformanceNote &StaffNoteDialog::note() const
{
    return draft_;
}
} // namespace singlilt
