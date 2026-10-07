// SPDX-License-Identifier: MIT
#pragma once
#include <QFileOpenEvent>
#include <QObject>
#include <QStringList>
#include <functional>
#include <utility>
namespace singlilt
{
// Finder can deliver documents before the main window exists.
class FileOpenHandler : public QObject
{
  public:
    explicit FileOpenHandler(QObject &application) : QObject(&application)
    {
        application.installEventFilter(this);
    }
    void setHandler(std::function<void(const QString &)> handler)
    {
        handler_ = std::move(handler);
        const auto pending = std::exchange(pending_, {});
        for (const auto &path : pending)
            handler_(path);
    }
    bool hasPendingFiles() const
    {
        return !pending_.isEmpty();
    }

  protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() != QEvent::FileOpen)
            return false;
        const auto *open = static_cast<QFileOpenEvent *>(event);
        const auto path = open->file().isEmpty() ? open->url().toLocalFile() : open->file();
        if (path.isEmpty())
            return false;
        if (handler_)
            handler_(path);
        else
            pending_.append(path);
        return true;
    }

  private:
    std::function<void(const QString &)> handler_;
    QStringList pending_;
};
} // namespace singlilt
