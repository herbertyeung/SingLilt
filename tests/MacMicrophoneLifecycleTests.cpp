#include "audio/MicrophoneCapture.h"
#include <QCoreApplication>
#include <QThread>
#include <stdexcept>
#include <thread>
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    for (int i = 0; i < 100; ++i)
    {
        singlilt::MicrophoneCapture capture;
        std::jthread analyzer(
            [&]
            {
                capture.stop();
                if (capture.state() != singlilt::MicrophoneCapture::State::Stopped)
                    throw std::runtime_error("stop status");
                capture.errorString();
                capture.takeBlocks();
            });
        analyzer.join();
        QCoreApplication::processEvents();
        capture.stop();
    }
    return 0;
}
