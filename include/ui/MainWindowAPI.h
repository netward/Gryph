#pragma once

#include <functional>

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "include/global/HTTPRequestHelper.hpp"
#include "include/stats/connections/connectionLister.hpp"


class QObject;
class QWidget;
class MainWindow;

enum class MwMessage;


namespace MainWindowApi
{
    // =========================================================
    // MainWindow lifetime
    // =========================================================

    void Initialize();


    void Detach(
        QWidget* expectedWindow
    );


    QWidget* Widget();

    // =========================================================
    // Generic lifetime-safe UI dispatch
    // =========================================================

    [[nodiscard]]
    bool Post(
        std::function<void(MainWindow&)> callback
    );

    // =========================================================
    // Global message bridges
    // =========================================================

    [[nodiscard]]
    bool DispatchMessage(
        MwMessage cmd,
        QStringList args
    );


    [[nodiscard]]
    bool DispatchDeeplink(
        QString url
    );


    [[nodiscard]]
    bool DispatchLog(
        QString log
    );


    // =========================================================
    // Existing MainWindow API
    // =========================================================

    void PrepareExit();


    void StartSelectMode(
        QObject* context,
        std::function<void(int)> callback
    );


    void RegisterHotkey(
        bool unregister
    );


    bool StopVpnProcess();


    void StopProfile(
        bool crash = false,
        bool block = false,
        bool manual = false
    );


    void RefreshStatus(
        const QString& trafficUpdate = {}
    );


    void UpdateTrafficGraph(
        int proxyDownload,
        int proxyUpload,
        int directDownload,
        int directUpload
    );


    void RefreshProxyList(
        const QList<int>& ids = {},
        bool mayNeedReset = false
    );


    void UpdateConnectionList(
        const QMap<
        QString,
        Stats::ConnectionMetadata
        >& toUpdate,

        const QMap<
        QString,
        Stats::ConnectionMetadata
        >& toAdd
    );


    void UpdateConnectionListWithRecreate(
        const QList<
        Stats::ConnectionMetadata
        >& connections
    );


    void SetDownloadReport(
        const Configs_network::
        DownloadProgressReport& report,

        bool show
    );


    void UpdateDataView(
        bool force = false
    );
}