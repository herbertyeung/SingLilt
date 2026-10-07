#include "platform/FileOpenHandler.h"
#include <QApplication>
#include <stdexcept>
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    singlilt::FileOpenHandler handler(app);
    QStringList files;
    QFileOpenEvent early("/tmp/My Song/练习.jpp");
    QApplication::sendEvent(&app, &early);
    if (!handler.hasPendingFiles())
        return 1;
    handler.setHandler([&](const QString &path) { files.append(path); });
    QFileOpenEvent later("/tmp/second.jpp");
    QApplication::sendEvent(&app, &later);
    if (files != QStringList{"/tmp/My Song/练习.jpp", "/tmp/second.jpp"})
        throw std::runtime_error("Finder file event lost");
}
