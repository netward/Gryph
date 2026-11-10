#include "include/sys/CoreSupervisor.hpp"

#include "include/global/Configs.hpp"
#include "include/ui/MainWindowAPI.h"

#include <QDebug>
#include <QThread>
#include <QTimer>

namespace Configs_sys {

    CoreSupervisor::CoreSupervisor(
        const QString& corePath,
        const QString& socketName,
        bool debugMode,
        QObject* parent
    )
        : CoreProcess(
            corePath,
            socketName,
            debugMode,
            parent
        )
    {
        connect(this, &QProcess::started, this,
            [this]()
            {
                pid_.store(static_cast<qint64>(QProcess::processId()), std::memory_order_release);

                if (lifecycleState() == State::Starting)
                {
                    transition(State::AwaitingIPC);
                }
            }
        );

        connect(this, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error)
            {
                if (error != QProcess::FailedToStart)
                {
                    return;
                }

                failedToStart_ = true;
                stopIntent_ = StopIntent::None;

                pid_.store(0, std::memory_order_release);

                MW_show_log("[CoreSupervisor] Start failed: " + errorString());

                transition(State::Failed);
            }
        );

        connect(this, &QProcess::finished, this, &CoreSupervisor::onProcessFinished);
    }

    CoreSupervisor::State
        CoreSupervisor::lifecycleState() const noexcept
    {
        return state_.load(std::memory_order_acquire);
    }

    qint64 CoreSupervisor::processId() const noexcept
    {
        return pid_.load(std::memory_order_acquire);
    }

    void CoreSupervisor::transition(State next)
    {
        Q_ASSERT(QThread::currentThread() == thread());

        const State previous = lifecycleState();

        if (previous == next)
        {
            return;
        }

        state_.store(next, std::memory_order_release);

        // Compatibility projection for existing RPC/UI code.
        // This is no longer an independent lifecycle state.
        Configs::dataManager->settingsRepo->core_running.store(next == State::Ready, std::memory_order_release);

        qInfo()
            << "[CoreSupervisor]"
            << static_cast<int>(previous)
            << "->"
            << static_cast<int>(next);

        emit lifecycleStateChanged(next);
    }

    void CoreSupervisor::Start()
    {
        Q_ASSERT(QThread::currentThread() == thread());

        const State current = lifecycleState();

        if (current == State::Starting || current == State::AwaitingIPC || current == State::Ready || current == State::Stopping)
        {
            return;
        }

        if (QProcess::state() != QProcess::NotRunning)
        {
            return;
        }

        ++recoveryTicket_;
        failedToStart_ = false;
        stopIntent_ = StopIntent::None;

        pid_.store(0, std::memory_order_release);

        transition(State::Starting);

        CoreProcess::Start();
    }

    void CoreSupervisor::ConfirmIpcReady(qint64 expectedPid)
    {
        Q_ASSERT(QThread::currentThread() == thread());

        if (expectedPid <= 0 || expectedPid != processId() || lifecycleState() != State::AwaitingIPC || QProcess::state() != QProcess::Running)
        {
            qWarning()
                << "[CoreSupervisor] Stale IPC ready event"
                << expectedPid;

            return;
        }

        transition(State::Ready);

        const int profileId = start_profile_when_core_is_up.exchange(-1, std::memory_order_acq_rel);

        // MainWindow receives CoreStarted through its lifetime-safe UI dispatcher.
        const bool delivered = MainWindowApi::DispatchMessage(MwMessage::CoreStarted, { QString::number(profileId) });

        if (!delivered)
        {
            MW_show_log("[CoreSupervisor] CoreStarted dispatch failed");
        }
    }

    void CoreSupervisor::scheduleRestart()
    {
        Q_ASSERT(QThread::currentThread() == thread());

        if (lifecycleState() == State::Recovering)
        {
            return;
        }

        transition(State::Recovering);

        const quint64 ticket = ++recoveryTicket_;

        QTimer::singleShot(200, this,
            [this, ticket]()
            {
                if (ticket != recoveryTicket_ || lifecycleState() != State::Recovering)
                {
                    return;
                }

                Start();
            }
        );
    }

    void CoreSupervisor::Restart()
    {
        Q_ASSERT(QThread::currentThread() == thread());

        const State current = lifecycleState();

        // A restart has already been requested.
        if (current == State::Recovering || current == State::Stopping)
        {
            return;
        }

        if (QProcess::state() == QProcess::NotRunning)
        {
            scheduleRestart();
            return;
        }

        stopIntent_ = StopIntent::Restart;

        transition(State::Stopping);

        // Existing force-kill behavior.
        // Graceful stop belongs to PR-5.2.
        CoreProcess::Kill();
    }

    void CoreSupervisor::Kill()
    {
        // Existing UI code uses Kill() to trigger a core restart, e.g. Restart Proxy.
        Restart();
    }

    void CoreSupervisor::StopForExit()
    {
        Q_ASSERT(QThread::currentThread() == thread());

        // Invalidate any pending scheduled restart.
        ++recoveryTicket_;

        stopIntent_ = StopIntent::Shutdown;

        start_profile_when_core_is_up.store(-1, std::memory_order_release);

        if (QProcess::state() == QProcess::NotRunning)
        {
            pid_.store(0, std::memory_order_release);

            transition(State::Stopped);
            return;
        }

        transition(State::Stopping);

        CoreProcess::Kill();
    }

    void CoreSupervisor::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
    {
        Q_ASSERT(QThread::currentThread() == thread());

        pid_.store(0, std::memory_order_release);

        const StopIntent intent = stopIntent_;
        stopIntent_ = StopIntent::None;

        if (intent == StopIntent::Shutdown)
        {
            transition(State::Stopped);
            return;
        }

        if (intent == StopIntent::Restart)
        {
            scheduleRestart();
            return;
        }

        if (failedToStart_)
        {
            transition(State::Failed);
            return;
        }

        // Unexpected process termination.
        MW_show_log(QString("[CoreSupervisor] Core exited: code=%1 status=%2").arg(exitCode).arg(static_cast<int>(exitStatus)));

        // Preserve the existing crash recovery action.
        MainWindowApi::StopProfile(true, true);

        // Preserve the existing anti-restart-loop policy.
        constexpr qint64 minCrashIntervalMs = 10000;

        if (lastCrash_.isValid() && lastCrash_.elapsed() < minCrashIntervalMs)
        {
            lastCrash_.invalidate();

            MW_show_log("[CoreSupervisor] Core exits too frequently; automatic recovery disabled");

            transition(State::Failed);
            return;
        }

        lastCrash_.start();

        start_profile_when_core_is_up.store(Configs::dataManager->settingsRepo->started_id, std::memory_order_release);

        scheduleRestart();
    }

} // namespace Configs_sys
