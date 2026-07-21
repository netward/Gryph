#pragma once

#include <functional>

#include <QList>
#include <QMap>
#include <QString>

#include "include/global/HTTPRequestHelper.hpp"
#include "include/stats/connections/connectionLister.hpp"

class QObject;
class QWidget;

namespace MainWindowApi
{
    // Создание главного окна.
    void Initialize();

    // Возвращает главное окно как QWidget.
    // Используется только как parent для диалогов.
    QWidget* Widget();

    // Завершение приложения.
    void PrepareExit();

    // Выбор профиля в главном окне.
    void StartSelectMode(
        QObject* context,
        std::function<void(int)> callback);

    // Регистрация или отключение глобальных горячих клавиш.
    void RegisterHotkey(bool unregister);

    // Управление VPN и профилями.
    bool StopVpnProcess();

    void StopProfile(
        bool crash = false,
        bool block = false,
        bool manual = false);

    // Обновление интерфейса.
    void RefreshStatus(const QString& trafficUpdate = {});

    void UpdateTrafficGraph(
        int proxyDownload,
        int proxyUpload,
        int directDownload,
        int directUpload);

    void RefreshProxyList(
        const QList<int>& ids = {},
        bool mayNeedReset = false);

    // Обновление списка соединений.
    void UpdateConnectionList(
        const QMap<QString, Stats::ConnectionMetadata>& toUpdate,
        const QMap<QString, Stats::ConnectionMetadata>& toAdd);

    void UpdateConnectionListWithRecreate(
        const QList<Stats::ConnectionMetadata>& connections);

    // Обновление панели загрузки.
    void SetDownloadReport(
        const Configs_network::DownloadProgressReport& report,
        bool show);

    void UpdateDataView(bool force = false);
}