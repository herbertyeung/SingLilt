// Native OMR process, cancellation, and multi-page integration checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "NativeStaffProcessCheck.h"
#include "recognition/LocalStaffRecognizer.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <chrono>
#include <stdexcept>
#include <thread>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace singlilt
{
namespace
{
void writeFixture(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("Cannot write the native process fixture");
}

QString literal(QString text)
{
    return "'" + text.replace("'", "''") + "'";
}

bool processEnded(int processId)
{
#ifdef Q_OS_WIN
    const HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, DWORD(processId));
    if (!handle)
        return processId > 0 && GetLastError() == ERROR_INVALID_PARAMETER;
    const bool ended = WaitForSingleObject(handle, 3000) == WAIT_OBJECT_0;
    CloseHandle(handle);
    return ended;
#elif defined(Q_OS_LINUX)
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 3000)
    {
        QFile status(QString("/proc/%1/stat").arg(processId));
        if (!status.open(QIODevice::ReadOnly))
            return processId > 0;
        const QByteArray contents = status.readAll();
        const auto endName = contents.lastIndexOf(')');
        if (endName >= 0 && contents.mid(endName + 2, 1) == "Z")
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
#else
    Q_UNUSED(processId);
    return true;
#endif
}
} // namespace

QJsonObject checkNativeStaffProcess()
{
    QJsonArray checks;
    const auto check = [&checks](bool passed, const char *name)
    {
        checks.append(QJsonObject{{"name", name}, {"passed", passed}});
        if (!passed)
            throw std::runtime_error(name);
    };
    try
    {
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary native fixtures");
        const auto root = temporary.path();
        const QString notation = "**kern\t**kern\n*clefF4\t*clefG2\n*k[]\t*k[]\n*M4/4\t*M4/4\n"
                                 "=1\t=1\n1C\t1c\n==\t==\n*-\t*-\n";
        writeFixture(root + "/model.gguf", "fixture model, not inference weights");
        QImage image(200, 200, QImage::Format_RGB32);
        image.fill(Qt::white);
        const auto options = [&](const QString &mode)
        {
            LocalStaffRecognitionOptions options;
#ifdef Q_OS_LINUX
            options.engineExecutable = QStandardPaths::findExecutable("python3");
            options.modelPath = root + "/model.gguf";
            options.timeoutSeconds = mode == "timeout" ? 5 : 15;
            const auto scriptPath = root + '/' + mode + ".py";
            QString script = "import sys,time,subprocess,pathlib\n";
            if (mode == "cancel" || mode == "timeout")
            {
                const auto pidPath = QString::fromUtf8(
                    QJsonDocument(QJsonArray{root + '/' + mode + ".pid"}).toJson(QJsonDocument::Compact));
                script += "child=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)'])\n"
                          "target=pathlib.Path(" +
                          pidPath +
                          "[0])\n"
                          "temporary=target.with_suffix('.tmp')\n"
                          "temporary.write_text(str(child.pid))\ntemporary.replace(target)\ntime.sleep(60)\n";
            }
            else if (mode == "nonzero")
                script += "sys.stderr.write('native fixture error')\nsys.exit(9)\n";
            else if (mode == "invalid")
                script += "print('invalid notation')\n";
            else if (mode != "empty")
                script += "sys.stdout.write(" +
                          QString::fromUtf8(QJsonDocument(QJsonArray{notation}).toJson(QJsonDocument::Compact)) +
                          "[0])\n";
            writeFixture(scriptPath, script.toUtf8());
            options.engineArgumentsPrefix = {scriptPath};
#else
            options.engineExecutable =
                qEnvironmentVariable("SystemRoot") + "/System32/WindowsPowerShell/v1.0/powershell.exe";
            options.modelPath = root + "/model.gguf";
            options.timeoutSeconds = mode == "timeout" ? 5 : 15;
            const auto scriptPath = root + '/' + mode + ".ps1";
            QString script = "$ErrorActionPreference='Stop'\n";
            if (mode == "cancel" || mode == "timeout")
            {
                script += "$start=New-Object Diagnostics.ProcessStartInfo\n"
                          "$start.FileName=$env:SystemRoot+'\\System32\\WindowsPowerShell\\v1.0\\powershell.exe'\n"
                          "$start.Arguments='-NoProfile -NonInteractive -Command \"Start-Sleep -Seconds 60\"'\n"
                          "$start.UseShellExecute=$false\n$start.CreateNoWindow=$true\n"
                          "$child=[Diagnostics.Process]::Start($start)\n"
                          "[IO.File]::WriteAllText(" +
                          literal(root + '/' + mode + ".pid.tmp") + ",($child.Id.ToString()))\n[IO.File]::Move(" +
                          literal(root + '/' + mode + ".pid.tmp") + "," + literal(root + '/' + mode + ".pid") +
                          ")\nStart-Sleep -Seconds 60\n";
            }
            else if (mode == "nonzero")
                script += "[Console]::Error.WriteLine('native fixture error')\nexit 9\n";
            else if (mode == "invalid")
                script += "[Console]::WriteLine('invalid notation')\n";
            else if (mode != "empty")
                script += "[Console]::Write(" + literal(notation) + ")\n";
            writeFixture(scriptPath, script.toUtf8());
            options.engineArgumentsPrefix = {"-NoProfile", "-NonInteractive", "-ExecutionPolicy",
                                             "Bypass",     "-File",           scriptPath};
#endif
            return options;
        };
        auto result = recognizeLocalStaff(image, "fixture.png", options("success"));
        check(result.valid(), "native stdout notation produces a candidate");
        check(result.originalImage == image && result.project->image == image,
              "native candidate preserves original image pixels");
        check(result.project->staffPerformance && !result.project->staffPerformance->notes.empty(),
              "native candidate retains full staff performance");
        check(result.sourceNotation.trimmed() == notation.toUtf8().trimmed(), "native source notation retained");

        const std::vector<StaffPageInput> pages{{image, "first", 0, image.rect()},
                                                {image, "second", 1, image.rect()}};
        result = recognizeLocalStaffPages(pages, options("success"));
        check(result.valid() && result.project->staffPages.size() == 2, "native multi-page request succeeds");
        check(result.project->staffPages[0].endTick == result.project->staffPages[1].startTick,
              "native page clocks remain contiguous");
        check(result.originalImages == std::vector<QImage>{image, image}, "native source page order retained");
        for (const auto &mode : {"nonzero", "invalid", "empty"})
        {
            result = recognizeLocalStaff(image, "fixture.png", options(mode));
            check(!result.valid() && !result.project && !result.error.isEmpty(),
                  "failed native output publishes no candidate");
        }
        auto invalid = options("success");
        invalid.timeoutSeconds = 0;
        check(!recognizeLocalStaff(image, "fixture.png", invalid).valid(), "invalid timeout rejected");
        invalid = options("success");
        invalid.modelPath = root + "/missing.gguf";
        check(!recognizeLocalStaff(image, "fixture.png", invalid).valid(), "missing model rejected");
        const std::atomic_bool alreadyCancelled{true};
        check(recognizeLocalStaff(image, "fixture.png", options("success"), &alreadyCancelled).cancelled,
              "pre-cancelled request never publishes a candidate");

        std::atomic_bool cancellation{false};
        const auto cancelOptions = options("cancel");
        std::jthread cancelThread(
            [&](std::stop_token stop)
            {
                while (!stop.stop_requested() && !QFileInfo::exists(root + "/cancel.pid"))
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!stop.stop_requested())
                    cancellation.store(true, std::memory_order_relaxed);
            });
        result = recognizeLocalStaff(image, "fixture.png", cancelOptions, &cancellation);
        cancelThread.request_stop();
        cancelThread.join();
        check(result.cancelled && !result.project, "running native request cancels");
        const auto childEnded = [&](const QString &mode)
        {
            QFile pid(root + '/' + mode + ".pid");
            return pid.open(QIODevice::ReadOnly) && processEnded(pid.readAll().trimmed().toInt());
        };
        check(childEnded("cancel"), "cancellation terminates the native process tree");
        result = recognizeLocalStaff(image, "fixture.png", options("timeout"));
        check(!result.valid() && !result.project && !result.error.isEmpty(), "native deadline produces failure");
        check(childEnded("timeout"), "timeout terminates the native process tree");
    }
    catch (const std::exception &error)
    {
        return {{"passed", false}, {"checks", checks}, {"error", QString::fromUtf8(error.what())}};
    }
    return {{"passed", true}, {"checks", checks}};
}
} // namespace singlilt
