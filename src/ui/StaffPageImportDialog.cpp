// Page order and split choices for staff-image import.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffPageImportDialog.h"
#include "i18n/LanguageManager.h"
#include "recognition/StaffPageSplitter.h"
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace singlilt
{
namespace
{
QListWidgetItem *pageItem(const StaffPageInput &page, int identity)
{
    auto *item = new QListWidgetItem(
        QIcon(QPixmap::fromImage(page.image.scaled(140, 175, Qt::KeepAspectRatio, Qt::SmoothTransformation))),
        page.label);
    item->setData(Qt::UserRole, identity);
    item->setToolTip(page.label);
    return item;
}
} // namespace
StaffPageImportDialog::StaffPageImportDialog(std::vector<StaffPageInput> pages, QWidget *parent)
    : QDialog(parent), pages_(std::move(pages))
{
    setObjectName("staffPageImportDialog");
    setWindowTitle(trText("ui.pages.import_title"));
    resize(680, 700);
    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(trText("ui.pages.import_help"), this);
    help->setWordWrap(true);
    layout->addWidget(help);
    list_ = new QListWidget(this);
    list_->setObjectName("staffPageOrder");
    list_->setIconSize({140, 175});
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    list_->setDefaultDropAction(Qt::MoveAction);
    for (std::size_t index = 0; index < pages_.size(); ++index)
        list_->addItem(pageItem(pages_[index], int(index)));
    list_->setCurrentRow(0);
    layout->addWidget(list_, 1);
    auto *actions = new QHBoxLayout;
    for (int offset : {-1, 1})
    {
        auto *move = new QPushButton(trText(offset < 0 ? "ui.pages.move_up" : "ui.pages.move_down"), this);
        move->setObjectName(offset < 0 ? "staffPageMoveUp" : "staffPageMoveDown");
        connect(move, &QPushButton::clicked, this, [this, offset] { moveSelected(offset); });
        actions->addWidget(move);
    }
    auto *split = new QPushButton(trText("ui.pages.split_selected"), this);
    split->setObjectName("staffPageSplitSelected");
    connect(split, &QPushButton::clicked, this, [this] { splitSelected(); });
    actions->addWidget(split);
    auto *remove = new QPushButton(trText("ui.pages.remove"), this);
    remove->setObjectName("staffPageRemove");
    connect(remove, &QPushButton::clicked, this,
            [this]
            {
                if (list_->count() > 1 && list_->currentRow() >= 0)
                    delete list_->takeItem(list_->currentRow());
                refreshLabels();
            });
    actions->addWidget(remove);
    layout->addLayout(actions);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("staffPageImportButtons");
    buttons->button(QDialogButtonBox::Ok)->setText(trText("ui.pages.start_recognition"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    refreshLabels();
}
void StaffPageImportDialog::moveSelected(int offset)
{
    const int from = list_->currentRow();
    const int to = from + offset;
    if (from < 0 || to < 0 || to >= list_->count())
        return;
    auto *item = list_->takeItem(from);
    list_->insertItem(to, item);
    list_->setCurrentRow(to);
    refreshLabels();
}
void StaffPageImportDialog::splitSelected()
{
    const int row = list_->currentRow();
    if (row < 0 || list_->count() >= 32)
        return;
    const auto identity = list_->item(row)->data(Qt::UserRole).toInt();
    const auto source = pages_[std::size_t(identity)];
    auto pieces =
        splitStaffPageInputs(source.image, source.label, source.sourceIndex, StaffPageSplitMode::LeftRight);
    if (pieces.size() != 2)
        return;
    delete list_->takeItem(row);
    for (int part = 0; part < 2; ++part)
    {
        pieces[std::size_t(part)].sourceRect.translate(source.sourceRect.topLeft());
        const int id = int(pages_.size());
        pages_.push_back(std::move(pieces[std::size_t(part)]));
        list_->insertItem(row + part, pageItem(pages_.back(), id));
    }
    list_->setCurrentRow(row);
    refreshLabels();
}
void StaffPageImportDialog::refreshLabels()
{
    for (int index = 0; index < list_->count(); ++index)
    {
        auto *item = list_->item(index);
        item->setText(trText("ui.pages.item")
                          .arg(index + 1)
                          .arg(pages_[std::size_t(item->data(Qt::UserRole).toInt())].label));
    }
}
std::vector<StaffPageInput> StaffPageImportDialog::orderedPages() const
{
    std::vector<StaffPageInput> pages;
    for (int index = 0; index < list_->count(); ++index)
        pages.push_back(pages_[std::size_t(list_->item(index)->data(Qt::UserRole).toInt())]);
    return pages;
}
} // namespace singlilt
