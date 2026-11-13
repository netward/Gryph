#pragma once

#include "include/sys/Process.hpp"

#include <QElapsedTimer>
#include <QtGlobal>

#include <atomic>

namespace Configs_sys {

    class CoreSupervisor final : public CoreProcess
    {
        Q_OBJECT

    public:
        enum class State
        {
            Stopped,
            Starting,
            AwaitingIPC,
            Ready,
            Stopping,
            Recovering,
            Failed
        };
        Q_ENUM(State)

            explicit CoreSupervisor(
                const QString& corePath,
                const QString& socketName,
                bool debugMode,
                QObject* parent = nullptr
            );

        // These commands must execute on the CoreSupervisor object's Qt thread.
        void Start();
        void Restart();

        // Compatibility with existing StopVPNProcess(): killing the core requests a restart.
        void Kill();

        // Explicit terminal shutdown, without restart.
        void StopForExit();

        // Called after PID verification and RPC adoption.
        [[nodiscard]]
        bool ConfirmIpcReady(qint64 expectedPid, quint64 expectedGeneration);

        [[nodiscard]]
        State lifecycleState() const noexcept;

        [[nodiscard]]
        quint64 processGeneration() const noexcept;

        // Thread-safe PID snapshot for MainWindow.
        [[nodiscard]]
        qint64 processId() const noexcept;

        // Profile to start once, after IPC becomes ready.
        std::atomic<int> start_profile_when_core_is_up{ -1 };

    signals:
        void lifecycleStateChanged(State newState);

    private:
        enum class StopIntent
        {
            None,
            Restart,
            Shutdown
        };

        void transition(State next);
        void scheduleRestart();
        void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

        std::atomic<State> state_{ State::Stopped };
        std::atomic<qint64> pid_{ 0 };
        std::atomic<quint64> processGeneration_{ 0 };

        StopIntent stopIntent_ = StopIntent::None;
        bool failedToStart_ = false;

        quint64 recoveryTicket_ = 0;
        QElapsedTimer lastCrash_;
    };

} // namespace Configs_sys
