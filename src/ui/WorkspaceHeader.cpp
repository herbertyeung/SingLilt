// Document identity, shared file actions, and workspace mode selection.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "WorkspaceHeader.h"
#include "i18n/LanguageManager.h"
#include <QAction>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QToolBar>
#include <QToolButton>

namespace singlilt
{
WorkspaceHeader::WorkspaceHeader(QWidget *parent) : QWidget(parent)
{
    setObjectName("workspaceHeader");
    setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(4, 6, 4, 6);
    row->setSpacing(16);
    document_ = new QWidget;
    document_->setObjectName("documentIdentity");
    auto *identityRow = new QHBoxLayout(document_);
    identityRow->setContentsMargins(0, 0, 0, 0);
    identityRow->setSpacing(10);
    title_ = new QLabel;
    title_->setObjectName("title");
    title_->setTextFormat(Qt::PlainText);
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title_->installEventFilter(this);
    identity_ = new QLabel;
    identity_->setObjectName("projectIdentity");
    identity_->setTextFormat(Qt::PlainText);
    identityRow->addWidget(title_, 1);
    identityRow->addWidget(identity_);
    row->addWidget(document_);
    commands_ = new QToolBar;
    commands_->setObjectName("documentCommands");
    commands_->setMovable(false);
    commands_->setFloatable(false);
    commands_->setIconSize(QSize(18, 18));
    commands_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    row->addWidget(commands_);
    row->addStretch();
    auto *mode = new QWidget;
    mode->setObjectName("workspaceMode");
    auto *modeRow = new QHBoxLayout(mode);
    modeRow->setContentsMargins(3, 3, 3, 3);
    modeRow->setSpacing(0);
    edit_ = new QPushButton;
    edit_->setObjectName("correctionModeButton");
    preview_ = new QPushButton;
    preview_->setObjectName("practiceModeButton");
    auto *group = new QButtonGroup(mode);
    for (auto *button : {edit_, preview_})
    {
        button->setCheckable(true);
        button->setProperty("modeSwitch", true);
        modeRow->addWidget(button);
    }
    group->addButton(edit_, 1);
    group->addButton(preview_, 0);
    preview_->setChecked(true);
    connect(group, &QButtonGroup::idClicked, this,
            [this](int mode)
            {
                if (modeRequested)
                    modeRequested(mode == 1);
            });
    row->addWidget(mode);
    more_ = new QToolButton(commands_);
    more_->setObjectName("documentMore");
    more_->setPopupMode(QToolButton::InstantPopup);
    more_->setMenu(new QMenu(more_));
    retranslate();
}

void WorkspaceHeader::setActions(QAction *create, QAction *save, QAction *undo, QAction *redo,
                                 const QList<QAction *> &secondary)
{
    // The menus own these actions; toolbar state and shortcuts use the same instances.
    const auto add = [this](QAction *action, const char *name)
    {
        commands_->addAction(action);
        auto *button = qobject_cast<QToolButton *>(commands_->widgetForAction(action));
        button->setObjectName(name);
        button->setAccessibleName(action->text());
        return button;
    };
    add(create, "editorNew");
    add(save, "editorSave")->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    commands_->addSeparator();
    add(undo, "editorUndo");
    add(redo, "editorRedo");
    for (auto *action : secondary)
        more_->menu()->addAction(action);
    commands_->addWidget(more_);
    retranslate();
}

void WorkspaceHeader::setDocument(const QString &title, const QString &path, bool modified)
{
    titleText_ = title;
    path_ = path;
    modified_ = modified;
    retranslate();
}

void WorkspaceHeader::setAuthoring(bool authoring)
{
    if (authoring_ == authoring)
        return;
    authoring_ = authoring;
    retranslate();
}

void WorkspaceHeader::retranslate()
{
    edit_->setText(trText(authoring_ ? "ui.editor.edit_mode" : "ui.product.correction_mode"));
    preview_->setText(trText(authoring_ ? "ui.editor.preview_mode" : "ui.product.practice_mode"));
    more_->setText(trText("ui.editor.more"));
    more_->setAccessibleName(more_->text());
    const char *state = path_.isEmpty() ? "ui.editor.unsaved"
                        : modified_     ? "ui.editor.modified"
                                        : "ui.editor.saved";
    identity_->setText(trText(state));
    identity_->setToolTip(path_.isEmpty() ? trText("ui.product.unsaved_project") : path_);
    identity_->setProperty("modified", modified_);
    for (auto *button : commands_->findChildren<QToolButton *>())
        if (button->defaultAction())
            button->setAccessibleName(button->defaultAction()->text());
    updateTitle();
}

void WorkspaceHeader::updateTitle()
{
    const QString fullTitle = titleText_.isEmpty() ? trText("messages.storage.untitled") : titleText_;
    const int preferredWidth =
        title_->fontMetrics().horizontalAdvance(fullTitle) + identity_->sizeHint().width() + 16;
    document_->setFixedWidth(qBound(150, preferredWidth, 420));
    title_->setToolTip(fullTitle);
    title_->setText(title_->fontMetrics().elidedText(fullTitle, Qt::ElideRight, title_->width()));
}

bool WorkspaceHeader::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == title_ && event->type() == QEvent::Resize)
        updateTitle();
    return QWidget::eventFilter(watched, event);
}

void WorkspaceHeader::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateTitle();
}
} // namespace singlilt
