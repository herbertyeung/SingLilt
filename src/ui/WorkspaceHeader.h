// Document identity, shared file actions, and workspace mode selection.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include <QWidget>
#include <functional>
class QAction;
class QLabel;
class QMenu;
class QPushButton;
class QToolBar;
class QToolButton;

namespace singlilt
{
class WorkspaceHeader final : public QWidget
{
  public:
    explicit WorkspaceHeader(QWidget *parent = nullptr);
    void setActions(QAction *create, QAction *save, QAction *undo, QAction *redo,
                    const QList<QAction *> &secondary);
    void setDocument(const QString &title, const QString &path, bool modified);
    void setAuthoring(bool authoring);
    void retranslate();
    QLabel *titleLabel() const
    {
        return title_;
    }
    QLabel *identityLabel() const
    {
        return identity_;
    }
    QPushButton *editButton() const
    {
        return edit_;
    }
    QPushButton *previewButton() const
    {
        return preview_;
    }
    std::function<void(bool)> modeRequested;

  protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void updateTitle();
    QWidget *document_;
    QLabel *title_;
    QLabel *identity_;
    QPushButton *edit_;
    QPushButton *preview_;
    QToolBar *commands_;
    QToolButton *more_;
    QString titleText_;
    QString path_;
    bool modified_ = false;
    bool authoring_ = false;
};
} // namespace singlilt
