#include "include/ui/MainWindowAPI.h"
#include "include/ui/MainWindow.h"
#include "include/global/Utils.hpp"

#include <functional>
#include <utility>

#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>

namespace
{
    // =========================================================
    // Current MainWindow registry
    //
    // QPointer automatically becomes nullptr when MainWindow
    // QObject is destroyed.
    // =========================================================

    QPointer<MainWindow>
        g_mainWindow;


    QMutex
        g_mainWindowMutex;


    // =========================================================
    // Asynchronous context-bound dispatch
    //
    // If called from UI thread:
    //     executes immediately.
    //
    // If called from worker thread:
    //     queues callback into MainWindow's thread.
    //
    // MainWindow is used as QObject context, so Qt discards the
    // queued callback if MainWindow is destroyed before delivery.
    // =========================================================

    bool dispatchToMainWindow(
        std::function<void(MainWindow&)> callback)
    {
        if (!callback)
        {
            return false;
        }


        MainWindow* target =
            nullptr;


        bool sameThread =
            false;


        {
            QMutexLocker locker(
                &g_mainWindowMutex
            );


            target =
                g_mainWindow.data();


            if (!target)
            {
                return false;
            }


            sameThread =
                target->thread()
                ==
                QThread::currentThread();


            // =================================================
            // Worker thread -> UI thread
            // =================================================

            if (!sameThread)
            {
                return QMetaObject::invokeMethod(
                    target,

                    [
                        target,
                        callback =
                        std::move(callback)
                    ]() mutable
                    {
                        callback(
                            *target
                        );
                    },

                    Qt::QueuedConnection
                );
            }
        }


        // =====================================================
        // Already in UI thread.
        //
        // Execute outside registry mutex so callback may itself
        // call MainWindowApi without deadlocking.
        // =====================================================

        callback(
            *target
        );


        return true;
    }


    // =========================================================
    // Synchronous boolean dispatch
    //
    // Required for APIs which must return a result immediately,
    // e.g. StopVpnProcess().
    //
    // UI thread:
    //     direct call.
    //
    // Worker thread:
    //     BlockingQueuedConnection.
    // =========================================================

    bool dispatchBoolToMainWindow(
        std::function<bool(MainWindow&)> callback,
        bool fallback = false)
    {
        if (!callback)
        {
            return fallback;
        }


        MainWindow* target =
            nullptr;


        bool sameThread =
            false;


        {
            QMutexLocker locker(
                &g_mainWindowMutex
            );


            target =
                g_mainWindow.data();


            if (!target)
            {
                return fallback;
            }


            sameThread =
                target->thread()
                ==
                QThread::currentThread();
        }


        // =====================================================
        // UI thread
        // =====================================================

        if (sameThread)
        {
            return callback(
                *target
            );
        }


        // =====================================================
        // Worker thread
        //
        // MainWindow is QObject context.
        // =====================================================

        bool result =
            fallback;


        const bool invoked =
            QMetaObject::invokeMethod(
                target,

                [
                    target,
                    callback =
                    std::move(callback),
                &result
                ]() mutable
                {
                    result =
                        callback(
                            *target
                        );
                },

                Qt::BlockingQueuedConnection
            );


        if (!invoked)
        {
            return fallback;
        }


        return result;
    }
}


// =============================================================
// MainWindowApi
// =============================================================

namespace MainWindowApi
{

    // =========================================================
    // Initialization
    // =========================================================

    void Initialize()
    {
        // -----------------------------------------------------
        // Check whether window already exists.
        // -----------------------------------------------------

        {
            QMutexLocker locker(
                &g_mainWindowMutex
            );


            if (g_mainWindow)
            {
                return;
            }
        }


        // -----------------------------------------------------
        // Do NOT construct MainWindow under registry mutex.
        //
        // MainWindow constructor initializes multiple services
        // which may indirectly use MainWindowApi.
        // -----------------------------------------------------

        auto* createdWindow =
            new MainWindow;


        // -----------------------------------------------------
        // Publish constructed window.
        // -----------------------------------------------------

        {
            QMutexLocker locker(
                &g_mainWindowMutex
            );


            if (g_mainWindow)
            {
                // Defensive protection against accidental
                // concurrent Initialize().
                createdWindow
                    ->deleteLater();

                return;
            }


            g_mainWindow =
                createdWindow;
        }

        // =========================================================
        // MainWindow is now completely constructed AND published.
        //
        // Replay everything which arrived while its constructor
        // was running.
        //
        // IMPORTANT:
        // this must be OUTSIDE g_mainWindowMutex.
        // =========================================================
        MW_FlushPendingMainWindowEvents();
    }


    // =========================================================
    // Lifetime detachment
    // =========================================================

    void Detach(
        QWidget* expectedWindow)
    {
        if (!expectedWindow)
        {
            return;
        }


        QMutexLocker locker(
            &g_mainWindowMutex
        );


        if (g_mainWindow.data()
            !=
            expectedWindow)
        {
            return;
        }


        g_mainWindow =
            nullptr;
    }

    // =========================================================
    // Raw QWidget access
    //
    // Use only as a short-lived dialog parent.
    // =========================================================
    QWidget* Widget()
    {
        QMutexLocker locker(
            &g_mainWindowMutex
        );


        return g_mainWindow.data();
    }

    // =========================================================
    // Global safe dispatch
    // =========================================================
    bool DispatchMessage(
        MwMessage cmd,
        QStringList args)
    {
        return dispatchToMainWindow(
            [
                cmd,
                args =
                std::move(args)
            ](MainWindow& mainWindow) mutable
            {
                mainWindow
                    .dispatchGlobalMessage(
                        cmd,
                        args
                    );
            }
        );
    }


    bool DispatchDeeplink(
        QString url)
    {
        return dispatchToMainWindow(
            [
                url =
                    std::move(url)
            ](MainWindow& mainWindow)
            {
                mainWindow
                    .dispatchGlobalDeeplink(
                        url
                    );
            }
                    );
    }


    bool DispatchLog(
        QString log)
    {
        return dispatchToMainWindow(
            [
                log =
                    std::move(log)
            ](MainWindow& mainWindow)
            {
                mainWindow
                    .enqueueGlobalLog(
                        log
                    );
            }
                    );
    }


    // =========================================================
    // Application shutdown
    // =========================================================

    void PrepareExit()
    {
        dispatchToMainWindow(
            [](MainWindow& mainWindow)
            {
                mainWindow
                    .prepare_exit();
            }
        );
    }


    // =========================================================
    // Select mode
    // =========================================================

    void StartSelectMode(
        QObject* context,
        std::function<void(int)> callback)
    {
        dispatchToMainWindow(
            [
                context,
                callback =
                std::move(callback)
            ](MainWindow& mainWindow) mutable
            {
                mainWindow
                    .start_select_mode(
                        context,
                        std::move(callback)
                    );
            }
        );
    }


    // =========================================================
    // Hotkey
    // =========================================================

    void RegisterHotkey(
        bool unregister)
    {
        dispatchToMainWindow(
            [
                unregister
            ](MainWindow& mainWindow)
            {
                mainWindow
                    .RegisterHotkey(
                        unregister
                    );
            }
                    );
    }


    // =========================================================
    // VPN process
    //
    // This one has a return value, therefore use synchronous
    // bool dispatcher.
    // =========================================================

    bool StopVpnProcess()
    {
        return dispatchBoolToMainWindow(
            [](MainWindow& mainWindow)
            {
                return mainWindow
                    .StopVPNProcess();
            },

            false
        );
    }


    // =========================================================
    // Profile stop
    // =========================================================

    void StopProfile(
        bool crash,
        bool block,
        bool manual)
    {
        dispatchToMainWindow(
            [
                crash,
                block,
                manual
            ](MainWindow& mainWindow)
            {
                mainWindow
                    .profile_stop(
                        crash,
                        block,
                        manual
                    );
            }
        );
    }


    // =========================================================
    // Status
    // =========================================================

    void RefreshStatus(
        const QString& trafficUpdate)
    {
        // Copy QString because the queued callback may execute
        // after this function has returned.

        const QString value =
            trafficUpdate;


        dispatchToMainWindow(
            [
                value
            ](MainWindow& mainWindow)
            {
                mainWindow
                    .refresh_status(
                        value
                    );
            }
                    );
    }


    // =========================================================
    // Traffic graph
    // =========================================================

    void UpdateTrafficGraph(
        int proxyDownload,
        int proxyUpload,
        int directDownload,
        int directUpload)
    {
        dispatchToMainWindow(
            [
                proxyDownload,
                proxyUpload,
                directDownload,
                directUpload
            ](MainWindow& mainWindow)
            {
                mainWindow
                    .update_traffic_graph(
                        proxyDownload,
                        proxyUpload,
                        directDownload,
                        directUpload
                    );
            }
        );
    }


    // =========================================================
    // Proxy list
    // =========================================================

    void RefreshProxyList(
        const QList<int>& ids,
        bool mayNeedReset)
    {
        // Must copy data because dispatch may be asynchronous.

        const QList<int> idsCopy =
            ids;


        dispatchToMainWindow(
            [
                idsCopy,
                mayNeedReset
            ](MainWindow& mainWindow)
            {
                mainWindow
                    .refresh_proxy_list(
                        idsCopy,
                        mayNeedReset
                    );
            }
        );
    }


    // =========================================================
    // Connection list
    // =========================================================

    void UpdateConnectionList(
        const QMap<
        QString,
        Stats::ConnectionMetadata
        >& toUpdate,

        const QMap<
        QString,
        Stats::ConnectionMetadata
        >& toAdd)
    {
        // Copy both containers because callback can be queued.

        const auto updateCopy =
            toUpdate;


        const auto addCopy =
            toAdd;


        dispatchToMainWindow(
            [
                updateCopy,
                addCopy
            ](MainWindow& mainWindow)
            {
                mainWindow
                    .UpdateConnectionList(
                        updateCopy,
                        addCopy
                    );
            }
        );
    }


    // =========================================================
    // Recreate connection list
    // =========================================================

    void UpdateConnectionListWithRecreate(
        const QList<
        Stats::ConnectionMetadata
        >& connections)
    {
        const auto connectionsCopy =
            connections;


        dispatchToMainWindow(
            [
                connectionsCopy
            ](MainWindow& mainWindow)
            {
                mainWindow
                    .UpdateConnectionListWithRecreate(
                        connectionsCopy
                    );
            }
                    );
    }


    // =========================================================
    // Download report
    // =========================================================

    void SetDownloadReport(
        const Configs_network::
        DownloadProgressReport& report,

        bool show)
    {
        const auto reportCopy =
            report;


        dispatchToMainWindow(
            [
                reportCopy,
                show
            ](MainWindow& mainWindow)
            {
                mainWindow
                    .setDownloadReport(
                        reportCopy,
                        show
                    );
            }
        );
    }


    // =========================================================
    // Data view
    // =========================================================

    void UpdateDataView(
        bool force)
    {
        dispatchToMainWindow(
            [
                force
            ](MainWindow& mainWindow)
            {
                mainWindow
                    .UpdateDataView(
                        force
                    );
            }
                    );
    }

} // namespace MainWindowApi