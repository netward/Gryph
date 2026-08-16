#include "include/ui/utils/DataViewHtmlGenerator.h"

#include <QMutexLocker>

#include "include/global/CountryHelper.hpp"
#include "include/global/Configs.hpp"


// =========================================================
// Download
// =========================================================

void DataViewHtmlGenerator::setDownloadReport(
    const DownloadProgressReport& report,
    bool show)
{
    QMutexLocker locker(
        &stateMutex_
    );


    download_.visible =
        show;


    download_.report =
        report;
}


// =========================================================
// Speed test
// =========================================================

void DataViewHtmlGenerator::seedSpeedTest(
    int totalProfiles)
{
    QMutexLocker locker(
        &stateMutex_
    );


    testProgress_ = 0;


    speedtest_.kind =
        Configs::dataManager
        ->settingsRepo
        ->speed_test_mode
        ==
        Configs::TestConfig::COUNTRY

        ? SpeedtestPanelState::
        Kind::Country

        : SpeedtestPanelState::
        Kind::Speed;


    speedtest_.totalProfiles =
        totalProfiles;


    speedtest_.visible =
        true;
}


void DataViewHtmlGenerator::setSpeedtestProgress(
    const QString& profileName,
    const libcore::SpeedTestResult& result)
{
    // -------------------------------------------------
    // Prepare expensive/string conversions before
    // acquiring our mutex.
    // -------------------------------------------------

    const QString dlSpeed =
        QString::fromStdString(
            result.dl_speed.value()
        );


    const QString ulSpeed =
        QString::fromStdString(
            result.ul_speed.value()
        );


    const QString serverCountry =
        QString::fromStdString(
            result.server_country.value()
        );


    const QString serverCountryFlag =
        CountryCodeToFlag(
            CountryNameToCode(
                serverCountry
            )
        );


    const QString serverName =
        QString::fromStdString(
            result.server_name.value()
        );


    // -------------------------------------------------
    // Commit the whole state atomically relative to
    // buildHtml().
    // -------------------------------------------------

    QMutexLocker locker(
        &stateMutex_
    );


    speedtest_.profileName =
        profileName;


    speedtest_.dlSpeed =
        dlSpeed;


    speedtest_.ulSpeed =
        ulSpeed;


    speedtest_.serverCountryFlag =
        serverCountryFlag;


    speedtest_.serverCountry =
        serverCountry;


    speedtest_.serverName =
        serverName;
}


// =========================================================
// URL / IP test
// =========================================================

void DataViewHtmlGenerator::seedLatencyTest(
    LatencyTestPanelState::Kind kind,
    int totalProfiles)
{
    QMutexLocker locker(
        &stateMutex_
    );


    testProgress_ = 0;


    latencyTest_.visible =
        true;


    latencyTest_.kind =
        kind;


    latencyTest_.totalProfiles =
        totalProfiles;
}


// =========================================================
// Clear
// =========================================================

void DataViewHtmlGenerator::clearTestSections()
{
    QMutexLocker locker(
        &stateMutex_
    );


    latencyTest_ = {};

    speedtest_ = {};

    testProgress_ = 0;
}


// =========================================================
// Progress
// =========================================================

void DataViewHtmlGenerator::addTestProgress(
    int count)
{
    QMutexLocker locker(
        &stateMutex_
    );


    testProgress_ +=
        count;
}


// =========================================================
// Render
// =========================================================

QString DataViewHtmlGenerator::buildHtml()
{
    // Hold the mutex for the whole render operation.
    //
    // Therefore the generated HTML always represents
    // one consistent state.
    QMutexLocker locker(
        &stateMutex_
    );


    QString html;


    if (download_.visible)
    {
        html +=
            downloadSectionHtml();
    }


    if (speedtest_.visible)
    {
        html +=
            speedtestSectionHtml();
    }


    if (latencyTest_.visible)
    {
        html +=
            latencyTestSectionHtml();
    }


    return html;
}


// =========================================================
// Progress bar
// =========================================================

QString DataViewHtmlGenerator::getProgressBar(
    long long current,
    long long total)
{
    qint64 count = 0;


    if (total > 0)
    {
        count =
            10 * current / total;
    }


    QString progressText;


    for (int i = 0;
        i < 10;
        ++i)
    {
        if (count--;
            count >= 0)
        {
            progressText +=
                "#";
        }
        else
        {
            progressText +=
                "-";
        }
    }


    return progressText;
}


// =========================================================
// Download HTML
//
// stateMutex_ must already be locked.
// =========================================================

QString
DataViewHtmlGenerator::downloadSectionHtml() const
{
    const auto progressText =
        getProgressBar(
            download_.report
            .downloadedSize,

            download_.report
            .totalSize
        );


    const QString stat =
        ReadableSize(
            download_.report
            .downloadedSize
        )
        +
        "/"
        +
        ReadableSize(
            download_.report
            .totalSize
        );


    return QString(
        "<p style='text-align:center;"
        "margin:0;'>"
        "Downloading %1: %2 %3"
        "</p>"
    )
        .arg(
            download_.report.fileName,
            stat,
            progressText
        );
}


// =========================================================
// Speedtest HTML
//
// stateMutex_ must already be locked.
// =========================================================

QString
DataViewHtmlGenerator::speedtestSectionHtml() const
{
    // -------------------------------------------------
    // Normal speed test
    // -------------------------------------------------

    if (speedtest_.kind ==
        SpeedtestPanelState::Kind::Speed)
    {
        QString firstLine =
            QStringLiteral(
                "Running Speedtest: %1"
            )
            .arg(
                speedtest_.profileName
            );


        if (speedtest_
            .totalProfiles > 1)
        {
            firstLine +=
                QString(
                    " (%1 / %2)"
                )
                .arg(
                    Int2String(
                        testProgress_
                    ),

                    Int2String(
                        speedtest_
                        .totalProfiles
                    )
                );
        }


        if (speedtest_
            .serverName
            .isEmpty())
        {
            return QString(
                "<p style='text-align:center;"
                "margin:0;'>"
                "%1"
                "</p>"
            )
                .arg(
                    firstLine
                );
        }


        return QString(
            "<p style='text-align:center;"
            "margin:0;'>"
            "%1"
            "</p>"

            "<div style='text-align:center;'>"

            "<span style='color:#3299FF;'>"
            "Dl↓ %2"
            "</span>  "

            "<span style='color:#86C43F;'>"
            "Ul↑ %3"
            "</span>"

            "</div>"

            "<p style='text-align:center;"
            "margin:0;'>"
            "Server: %4%5, %6"
            "</p>"
        )
            .arg(
                firstLine,

                speedtest_.dlSpeed,
                speedtest_.ulSpeed,

                speedtest_
                .serverCountryFlag,

                speedtest_
                .serverCountry,

                speedtest_
                .serverName
            );
    }


    // -------------------------------------------------
    // Country test
    // -------------------------------------------------

    QString result;


    QString content =
        QStringLiteral(
            "Running Country Test"
        );


    if (speedtest_
        .totalProfiles > 1)
    {
        const int total =
            speedtest_
            .totalProfiles;


        const int progress =
            testProgress_;


        QString progressText =
            getProgressBar(
                progress,
                total
            );


        const int percentage =
            total > 0
            ? 100 * progress / total
            : 0;


        progressText +=
            QString(" %1%")
            .arg(
                percentage
            );


        result =
            QString(
                "<p style='text-align:center;"
                "margin:0;'>"
                "%1"
                "</p>"
            )
            .arg(
                progressText
            );


        content +=
            QString(
                " (%1 / %2)"
            )
            .arg(
                Int2String(
                    progress
                ),

                Int2String(
                    total
                )
            );
    }


    result +=
        QString(
            "<p style='text-align:center;"
            "margin:0;'>"
            "%1"
            "</p>"
        )
        .arg(
            content
        );


    return result;
}


// =========================================================
// URL/IP HTML
//
// stateMutex_ must already be locked.
// =========================================================

QString
DataViewHtmlGenerator::latencyTestSectionHtml() const
{
    QString result;


    QString content =
        latencyTest_.kind ==
        LatencyTestPanelState::Kind::Url

        ? QStringLiteral(
            "Running URL test"
        )

        : QStringLiteral(
            "Running IP test"
        );


    if (latencyTest_
        .totalProfiles > 1)
    {
        const int total =
            latencyTest_
            .totalProfiles;


        const int progress =
            testProgress_;


        QString progressText =
            getProgressBar(
                progress,
                total
            );


        const int percentage =
            total > 0
            ? 100 * progress / total
            : 0;


        progressText +=
            QString(" %1%")
            .arg(
                percentage
            );


        result =
            QString(
                "<p style='text-align:center;"
                "margin:0;'>"
                "%1"
                "</p>"
            )
            .arg(
                progressText
            );


        content +=
            QString(
                " (%1 / %2)"
            )
            .arg(
                Int2String(
                    progress
                ),

                Int2String(
                    total
                )
            );
    }


    result +=
        QString(
            "<p style='text-align:center;"
            "margin:0;'>"
            "%1"
            "</p>"
        )
        .arg(
            content
        );


    return result;
}