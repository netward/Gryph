#pragma once

#include <QString>
#include <QIcon>
#include <QMutex>

#include "include/global/HTTPRequestHelper.hpp"

#ifndef Q_MOC_RUN
#include <core/server/gen/libcore.pb.h>
#endif


class DataViewHtmlGenerator
{
public:

    struct DownloadPanelState
    {
        bool visible = false;

        DownloadProgressReport report;
    };


    struct SpeedtestPanelState
    {
        enum class Kind
        {
            Speed,
            Country
        };


        bool visible = false;

        Kind kind =
            Kind::Speed;


        QString profileName;

        QString dlSpeed;
        QString ulSpeed;

        QString serverCountryFlag;
        QString serverCountry;
        QString serverName;


        int totalProfiles = 0;
    };


    struct LatencyTestPanelState
    {
        enum class Kind
        {
            Url,
            Ip
        };


        bool visible = false;

        Kind kind =
            Kind::Url;


        int totalProfiles = 0;
    };


    // -------------------------------------------------
    // Thread-safe state modification
    // -------------------------------------------------

    void setDownloadReport(
        const DownloadProgressReport& report,
        bool show
    );


    void seedSpeedTest(
        int totalProfiles
    );


    void setSpeedtestProgress(
        const QString& profileName,
        const libcore::SpeedTestResult& result
    );


    void seedLatencyTest(
        LatencyTestPanelState::Kind kind,
        int totalProfiles
    );


    void clearTestSections();


    void addTestProgress(
        int count = 1
    );


    // -------------------------------------------------
    // Thread-safe rendering
    // -------------------------------------------------

    [[nodiscard]]
    QString buildHtml();


private:

    static QString getProgressBar(
        long long current,
        long long total
    );


    // These methods must only be called while
    // stateMutex_ is locked.
    [[nodiscard]]
    QString downloadSectionHtml() const;


    [[nodiscard]]
    QString speedtestSectionHtml() const;


    [[nodiscard]]
    QString latencyTestSectionHtml() const;


private:

    // Protects ALL state below.
    //
    // A single mutex guarantees that buildHtml()
    // sees one consistent state instead of a mix
    // of values from different worker updates.
    mutable QMutex stateMutex_;


    DownloadPanelState
        download_{};


    SpeedtestPanelState
        speedtest_{};


    LatencyTestPanelState
        latencyTest_{};


    // No atomic is required anymore.
    //
    // This value is protected by stateMutex_
    // together with the rest of the state.
    int testProgress_ = 0;
};