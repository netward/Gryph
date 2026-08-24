#include "include/ui/MainWindowAPI.h"
#include "include/ui/MainWindow.h"
#include "include/global/Utils.hpp"

#include <functional>
#include <utility>

#include <QDebug>
#include <QCoreApplication>
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
    // MainWindow instance generation
    //
    // Incremented every time MainWindow is published or
    // detached.
    //
    // A queued callback remembers the generation which existed
    // when it was submitted. If the window is replaced before
    // delivery, that callback is discarded.
    // =========================================================

    quint64
        g_mainWindowGeneration =
        0;

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
        std::function<void(MainWindow&)> callback
    )
    {
        // =========================================================
        // Validate callback
        // =========================================================

        if (!callback)
        {
            return false;
        }


        QCoreApplication* const application =
            QCoreApplication::instance();


        // =========================================================
        // Application is already shutting down.
        //
        // Do not enqueue any new MainWindow work.
        // =========================================================

        if (!application ||
            QCoreApplication::closingDown())
        {
            return false;
        }


        QThread* const uiThread =
            application->thread();


        if (!uiThread)
        {
            return false;
        }


        // =========================================================
        // Already running in the UI thread
        // =========================================================

        if (QThread::currentThread()
            ==
            uiThread)
        {
            QPointer<MainWindow>
                target;


            {
                QMutexLocker locker(
                    &g_mainWindowMutex
                );


                target =
                    g_mainWindow;
            }


            if (!target)
            {
                return false;
            }


            // UI thread cannot simultaneously destroy MainWindow
            // while this code is synchronously executing.
            callback(
                *target
            );


            return true;
        }


        // =========================================================
        // Worker thread
        //
        // IMPORTANT:
        //
        // Do NOT dereference MainWindow here.
        // Do NOT call target->thread().
        // Do NOT invoke directly against MainWindow.
        //
        // We only take a snapshot of the registry generation.
        // =========================================================

        quint64 expectedGeneration =
            0;


        {
            QMutexLocker locker(
                &g_mainWindowMutex
            );


            if (!g_mainWindow)
            {
                return false;
            }


            expectedGeneration =
                g_mainWindowGeneration;
        }


        // =========================================================
        // Worker -> UI
        //
        // QCoreApplication is used as the QObject context.
        //
        // Therefore actual MainWindow lookup happens only after
        // execution reaches the UI thread.
        // =========================================================

        return QMetaObject::invokeMethod(
            application,

            [
                expectedGeneration,

                callback =
                std::move(callback)
            ]() mutable
            {
                // We are now in the UI thread.

                QPointer<MainWindow>
                    target;


                {
                    QMutexLocker locker(
                        &g_mainWindowMutex
                    );


                    // ---------------------------------------------
                    // Window was destroyed/replaced while this
                    // callback was waiting in the event queue.
                    // ---------------------------------------------

                    if (g_mainWindowGeneration
                        !=
                        expectedGeneration)
                    {
                        return;
                    }


                    target =
                        g_mainWindow;
                }


                if (!target)
                {
                    return;
                }


                callback(
                    *target
                );
            },

            Qt::QueuedConnection
        );
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

        QCoreApplication* const application =
            QCoreApplication::instance();


        if (!application)
        {
            qCritical()
                << "MainWindowApi::Initialize(): "
                "QCoreApplication does not exist.";

            return;
        }


        if (QThread::currentThread()
            !=
            application->thread())
        {
            qCritical()
                << "MainWindowApi::Initialize(): "
                "must be called from UI thread.";

            return;
        }

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
            // New published MainWindow instance.
            ++g_mainWindowGeneration;
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

    bool Post(
        std::function<void(MainWindow&)> callback
    )
    {
        return dispatchToMainWindow(
            std::move(callback)
        );
    }

    // =========================================================
    // Lifetime detachment
    // =========================================================

    void Detach(
        QWidget* expectedWindow
    )
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


        // ---------------------------------------------------------
        // Invalidate all callbacks submitted for this MainWindow.
        // ---------------------------------------------------------

        ++g_mainWindowGeneration;
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
        // =========================================================
        // Validation
        // =========================================================

        if (!context ||
            !callback)
        {
            return;
        }


        // =========================================================
        // Lifetime guard
        //
        // The original QObject* is valid at the moment this API is
        // called, but dispatchToMainWindow() may queue the operation.
        //
        // QPointer will automatically become nullptr if context is
        // destroyed before queued delivery.
        // =========================================================

        const QPointer<QObject> safeContext(
            context
        );


        // =========================================================
        // NEVER capture raw QObject* across queued dispatch.
        // =========================================================

        dispatchToMainWindow(
            [
                safeContext,

                callback =
                std::move(callback)
            ](MainWindow& mainWindow) mutable
            {
                // =================================================
                // Context was destroyed while this invocation was
                // waiting in the event queue.
                //
                // Do not enter select mode and do not dereference it.
                // =================================================

                if (!safeContext)
                {
                    return;
                }


                // =================================================
                // From this point MainWindow::start_select_mode()
                // uses this object as QObject::connect receiver.
                //
                // Qt will automatically disconnect the callback if
                // the receiver is destroyed later.
                // =================================================

                mainWindow.start_select_mode(
                    safeContext.data(),
                    callback
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
        // =========================================================
        // IMPORTANT:
        //
        // Do not synchronously block a worker waiting for the UI.
        //
        // MainWindow::StopVPNProcess() may itself communicate with
        // DS_cores, so BlockingQueuedConnection here could create:
        //
        // DS_cores -> UI -> DS_cores
        //
        // dispatchToMainWindow() is asynchronous for worker callers.
        // =========================================================

        return dispatchToMainWindow(
            [](MainWindow& mainWindow)
            {
                (void)mainWindow
                    .StopVPNProcess();
            }
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