// Staff-recognition subprocess deadlines and cancellation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QElapsedTimer>
#include <QProcess>
#include <QThread>
#include <vector>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace singlilt
{
#ifdef Q_OS_WIN
class StaffProcessJob
{
  public:
    StaffProcessJob() = default;
    StaffProcessJob(const StaffProcessJob &) = delete;
    StaffProcessJob &operator=(const StaffProcessJob &) = delete;
    ~StaffProcessJob()
    {
        if (attributesInitialized_)
            DeleteProcThreadAttributeList(startup_.lpAttributeList);
        if (job_)
            CloseHandle(job_);
    }

    bool configure(QProcess &process)
    {
        job_ = CreateJobObjectW(nullptr, nullptr);
        if (!job_)
            return false;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            return false;
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (bytes == 0)
            return false;
        attributeStorage_.resize(bytes);
        startup_.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage_.data());
        if (!InitializeProcThreadAttributeList(startup_.lpAttributeList, 1, 0, &bytes))
            return false;
        attributesInitialized_ = true;
        if (!UpdateProcThreadAttribute(startup_.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job_,
                                       sizeof(job_), nullptr, nullptr))
            return false;
        process.setCreateProcessArgumentsModifier(
            [this](QProcess::CreateProcessArguments *arguments)
            {
                // Atomically joining the job prevents an engine from spawning an untracked child.
                startup_.StartupInfo = *arguments->startupInfo;
                startup_.StartupInfo.cb = sizeof(startup_);
                arguments->startupInfo = &startup_.StartupInfo;
                arguments->flags |= EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW;
            });
        return true;
    }

    void terminate()
    {
        if (job_)
            TerminateJobObject(job_, 1);
    }

    bool waitForEmpty(int milliseconds)
    {
        if (!job_)
            return true;
        QElapsedTimer elapsed;
        elapsed.start();
        do
        {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            if (!QueryInformationJobObject(job_, JobObjectBasicAccountingInformation, &accounting,
                                           sizeof(accounting), nullptr))
                return false;
            if (accounting.ActiveProcesses == 0)
                return true;
            QThread::msleep(10);
        } while (elapsed.elapsed() < milliseconds);
        return false;
    }

  private:
    HANDLE job_ = nullptr;
    STARTUPINFOEXW startup_{};
    std::vector<unsigned char> attributeStorage_;
    bool attributesInitialized_ = false;
};
#endif
} // namespace singlilt
