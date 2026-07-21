#include "include/ui/mainwindowapi.h"
#include "include/ui/mainwindow.h"

#include <QPointer>
#include <utility>

namespace
{
    QPointer<MainWindow> g_mainWindow;

    MainWindow* window()
    {
        return g_mainWindow.data();
    }
}

namespace MainWindowApi
{
    void Initialize()
    {
        if (g_mainWindow) {
            return;
        }

        g_mainWindow = new MainWindow;
    }

    QWidget* Widget()
    {
        return g_mainWindow.data();
    }

    void PrepareExit()
    {
        if (auto* mainWindow = window()) {
            mainWindow->prepare_exit();
        }
    }

    void StartSelectMode(
        QObject* context,
        std::function<void(int)> callback)
    {
        if (auto* mainWindow = window()) {
            mainWindow->start_select_mode(
                context,
                std::move(callback));
        }
    }

    void RegisterHotkey(bool unregister)
    {
        if (auto* mainWindow = window()) {
            mainWindow->RegisterHotkey(unregister);
        }
    }

    bool StopVpnProcess()
    {
        if (auto* mainWindow = window()) {
            return mainWindow->StopVPNProcess();
        }

        return false;
    }

    void StopProfile(
        bool crash,
        bool block,
        bool manual)
    {
        if (auto* mainWindow = window()) {
            mainWindow->profile_stop(
                crash,
                block,
                manual);
        }
    }

    void RefreshStatus(const QString& trafficUpdate)
    {
        if (auto* mainWindow = window()) {
            mainWindow->refresh_status(trafficUpdate);
        }
    }

    void UpdateTrafficGraph(
        int proxyDownload,
        int proxyUpload,
        int directDownload,
        int directUpload)
    {
        if (auto* mainWindow = window()) {
            mainWindow->update_traffic_graph(
                proxyDownload,
                proxyUpload,
                directDownload,
                directUpload);
        }
    }

    void RefreshProxyList(
        const QList<int>& ids,
        bool mayNeedReset)
    {
        if (auto* mainWindow = window()) {
            mainWindow->refresh_proxy_list(
                ids,
                mayNeedReset);
        }
    }

    void UpdateConnectionList(
        const QMap<QString, Stats::ConnectionMetadata>& toUpdate,
        const QMap<QString, Stats::ConnectionMetadata>& toAdd)
    {
        if (auto* mainWindow = window()) {
            mainWindow->UpdateConnectionList(
                toUpdate,
                toAdd);
        }
    }

    void UpdateConnectionListWithRecreate(
        const QList<Stats::ConnectionMetadata>& connections)
    {
        if (auto* mainWindow = window()) {
            mainWindow->UpdateConnectionListWithRecreate(
                connections);
        }
    }

    void SetDownloadReport(
        const Configs_network::DownloadProgressReport& report,
        bool show)
    {
        if (auto* mainWindow = window()) {
            mainWindow->setDownloadReport(
                report,
                show);
        }
    }

    void UpdateDataView(bool force)
    {
        if (auto* mainWindow = window()) {
            mainWindow->UpdateDataView(force);
        }
    }
}