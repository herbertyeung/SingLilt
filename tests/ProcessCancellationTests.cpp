#include "recognition/StaffProcessJob.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    QString pidFile = tmp.filePath("child.pid");
    QProcess process;
    singlilt::StaffProcessJob job;
    if (!job.configure(process))
        return 1;
    process.start("/bin/sh", {"-c", "sleep 60 & echo $! > '" + pidFile + "'; wait"});
    if (!process.waitForStarted(1000))
        return 2;
    QElapsedTimer deadline;
    deadline.start();
    while (!QFile::exists(pidFile) && deadline.elapsed() < 2000)
        QThread::msleep(10);
    QFile file(pidFile);
    if (!file.open(QIODevice::ReadOnly))
        return 3;
    pid_t child = file.readAll().trimmed().toInt();
    job.terminate();
    process.waitForFinished(2000);
    for (int i = 0; i < 200 && kill(child, 0) == 0; ++i)
        QThread::msleep(10);
    if (process.state() != QProcess::NotRunning)
        throw std::runtime_error("parent alive");
    if (kill(child, 0) == 0)
    {
        QProcess status;
        status.start("/bin/ps", {"-o", "stat=", "-p", QString::number(child)});
        status.waitForFinished(1000);
        if (!status.readAllStandardOutput().trimmed().startsWith('Z'))
            throw std::runtime_error("child alive");
    }
    std::cout << "PASS cancellation kills parent and descendant\n";
}
