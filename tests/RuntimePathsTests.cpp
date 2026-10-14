#include "platform/RuntimePaths.h"
#include <QCoreApplication>
#include <stdexcept>
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace singlilt;
    if (runtimeResourceDirectory("/tmp/My App.app/Contents/MacOS") != "/tmp/My App.app/Contents/Resources")
        throw std::runtime_error("bundle resources");
    if (runtimeResourceDirectory("/tmp/fixture") != "/tmp/fixture")
        throw std::runtime_error("flat fixture");
#ifdef Q_OS_WIN
    if (helperExecutableName("engine") != "engine.exe")
        return 1;
#else
    if (helperExecutableName("engine") != "engine")
        return 1;
#endif
}
