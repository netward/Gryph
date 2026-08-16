#include "include/ui/mainwindow.h"
#include "ui_mainwindow.h"

#include "include/stats/traffic/TrafficLooper.hpp"
#include "include/api/RPC.h"
#include "include/ui/utils//MessageBoxTimer.h"
#include "3rdparty/qv2ray/v2/proxy/QvProxyConfigurator.hpp"

#include <QInputDialog>
#include <QPushButton>
#include <QDesktopServices>
#include <QMessageBox>
#include <QJsonDocument>

#include "include/configs/generate.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"

#include "include/sys/Process.hpp"

#include <algorithm>

#include <memory>

// rpc

using namespace API;

namespace
{
    class AtomicFlagResetGuard final
    {
    public:
        explicit AtomicFlagResetGuard(
            std::atomic_bool& flag)
            :
            flag_(flag)
        {}


        ~AtomicFlagResetGuard()
        {
            flag_.store(
                false,
                std::memory_order_release
            );
        }


        AtomicFlagResetGuard(
            const AtomicFlagResetGuard&) = delete;

        AtomicFlagResetGuard&
            operator=(
                const AtomicFlagResetGuard&) = delete;


    private:
        std::atomic_bool& flag_;
    };


    class SemaphoreReleaseGuard final
    {
    public:
        explicit SemaphoreReleaseGuard(
            QSemaphore& semaphore)
            :
            semaphore_(semaphore)
        {}


        ~SemaphoreReleaseGuard()
        {
            semaphore_.release();
        }


        SemaphoreReleaseGuard(
            const SemaphoreReleaseGuard&) = delete;

        SemaphoreReleaseGuard&
            operator=(
                const SemaphoreReleaseGuard&) = delete;


    private:
        QSemaphore& semaphore_;
    };
}

void MainWindow::setup_rpc(QLocalSocket *socket) {
    // The Client is long-lived and never recreated; on core restart we only
    // swap the underlying connection so worker threads holding `defaultClient`
    // never touch freed memory.
    QMutexLocker lock(&defaultClientMutex);
    if (defaultClient == nullptr) {
        defaultClient = new Client();
    }
    defaultClient->Reconnect(socket);

    // Loopers run for the lifetime of the app, start only once
    if (!rpc_started) {
        rpc_started = true;
        runOnNewThread([=] { Stats::trafficLooper->Loop(); });
        runOnNewThread([=] { Stats::connection_lister->Loop(); });
    }
}

void MainWindow::runURLTest(const QString& config, const QString& xrayConfig, bool useDefault, const QStringList& outboundTags, const QMap<QString, int>& tag2entID, int entID) {
    if (stopSpeedtest.load()) {
        MW_show_log(tr("Profile test aborted"));
        return;
    }

    libcore::TestReq req;
    for (const auto &item: outboundTags) {
        req.outbound_tags.push_back(item.toStdString());
    }
    req.config = config.toStdString();
    req.url = Configs::dataManager->settingsRepo->test_latency_url.toStdString();
    req.use_default_outbound = useDefault;
    req.max_concurrency = Configs::dataManager->settingsRepo->test_concurrent;
    req.test_timeout_ms = Configs::dataManager->settingsRepo->url_test_timeout_ms;
    req.xray_config = xrayConfig.toStdString();
    req.need_xray = !xrayConfig.isEmpty();

    auto done = new QMutex;
    done->lock();
    runOnNewThread([=,this]
    {
        bool ok;
        while (true)
        {
            QThread::msleep(200);
            if (done->try_lock()) break;
            auto resp = defaultClient->QueryURLTest(&ok);
            if (!ok || resp.results.empty())
            {
                continue;
            }

            bool needRefresh = false;
            QList<int> profileIDs;
            for (const auto& res : resp.results)
            {
                dataViewHtmlGenerator_.addTestProgress();
                UpdateDataView();
                int entid = -1;
                if (!tag2entID.empty()) {
                    entid = tag2entID.count(QString::fromStdString(res.outbound_tag.value())) == 0 ? -1 : tag2entID[QString::fromStdString(res.outbound_tag.value())];
                }
                if (entid == -1) {
                    continue;
                }
                profileIDs << entID;
                auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
                if (ent == nullptr) {
                    continue;
                }
                if (res.error.value().empty()) {

                    ent->SetLatency(
                        res.latency_ms.value()
                    );
                }

                else {

                    const QString error =
                        QString::fromStdString(
                            res.error.value()
                        );


                    if (error.contains(
                        "test aborted")
                        ||
                        error.contains(
                            "context canceled"))
                    {
                        ent->SetLatency(0);
                    }

                    else {

                        ent->SetLatency(-1);


                        MW_show_log(
                            tr("[%1] test error: %2")
                            .arg(
                                ent->outbound
                                ->DisplayTypeAndName(),
                                error
                            )
                        );
                    }
                }
                Configs::dataManager->profilesRepo->Save(ent);
                needRefresh = true;
            }
            if (needRefresh)
            {
                UpdateDataView(true);
                runOnUiThread([=,this]{
                    refresh_proxy_list(profileIDs);
                });
            }
        }
        done->unlock();
        delete done;
    });
    bool rpcOK;
    auto result = defaultClient->Test(&rpcOK, req);
    done->unlock();
    //
    if (!rpcOK || result.results.empty()) return;

    for (const auto &res: result.results) {
        if (!tag2entID.empty()) {
            entID = tag2entID.count(QString::fromStdString(res.outbound_tag.value())) == 0 ? -1 : tag2entID[QString::fromStdString(res.outbound_tag.value())];
        }
        if (entID == -1) {
            MW_show_log(tr("Something is very wrong, the subject ent cannot be found!"));
            continue;
        }

        auto ent = Configs::dataManager->profilesRepo->GetProfile(entID);
        if (ent == nullptr) {
            MW_show_log(tr("Profile manager data is corrupted, try again."));
            continue;
        }

        if (res.error.value().empty()) {

            ent->SetLatency(
                res.latency_ms.value()
            );
        }

        else {

            const QString error =
                QString::fromStdString(
                    res.error.value()
                );


            if (error.contains(
                "test aborted")
                ||
                error.contains(
                    "context canceled"))
            {
                ent->SetLatency(0);
            }

            else {

                ent->SetLatency(-1);


                MW_show_log(
                    tr("[%1] test error: %2")
                    .arg(
                        ent->outbound
                        ->DisplayTypeAndName(),
                        error
                    )
                );
            }
        }
        Configs::dataManager->profilesRepo->Save(ent);
    }
}

void MainWindow::runIPTest(const QString& config, const QString& xrayConfig, bool useDefault, const QStringList& outboundTags, const QMap<QString, int>& tag2entID, int entID) {
    if (stopSpeedtest.load()) {
        MW_show_log(tr("Profile test aborted"));
        return;
    }

    libcore::IPTestRequest req;
    for (const auto &item: outboundTags) {
        req.outbound_tags.push_back(item.toStdString());
    }
    req.config = config.toStdString();
    req.use_default_outbound = useDefault;
    req.max_concurrency = Configs::dataManager->settingsRepo->test_concurrent;
    req.test_timeout_ms = Configs::dataManager->settingsRepo->url_test_timeout_ms;
    req.xray_config = xrayConfig.toStdString();
    req.need_xray = !xrayConfig.isEmpty();

    auto done = new QMutex;
    done->lock();
    runOnNewThread([=,this]
    {
        bool ok;
        while (true)
        {
            QThread::msleep(200);
            if (done->try_lock()) break;
            auto resp = defaultClient->QueryIPTest(&ok);
            if (!ok || resp.results.empty())
            {
                continue;
            }

            bool needRefresh = false;
            QList<int> profileIDs;
            for (const auto& res : resp.results)
            {
                dataViewHtmlGenerator_.addTestProgress();
                UpdateDataView();
                int entid = -1;
                if (!tag2entID.empty()) {
                    entid = tag2entID.count(QString::fromStdString(res.outbound_tag.value())) == 0 ? -1 : tag2entID[QString::fromStdString(res.outbound_tag.value())];
                }
                if (entid == -1) {
                    continue;
                }
                profileIDs << entid;
                auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
                if (ent == nullptr) {
                    continue;
                }
                if (res.error.value().empty()) {
                    ent->SetIpTestResult(
                        QString::fromStdString(
                            res.ip.value()
                        ),

                        QString::fromStdString(
                            res.country_code.value()
                        )
                    );
                } else {
                    if (!QString::fromStdString(res.error.value()).contains("test aborted") &&
                        !QString::fromStdString(res.error.value()).contains("context canceled")) {
                        MW_show_log(tr("[%1] IP test error: %2").arg(ent->outbound->DisplayTypeAndName(), QString::fromStdString(res.error.value())));
                    }
                    ent->ClearIpTestResult();
                }
                Configs::dataManager->profilesRepo->Save(ent);
                needRefresh = true;
            }
            if (needRefresh)
            {
                UpdateDataView(true);
                runOnUiThread([=,this]{
                    refresh_proxy_list(profileIDs);
                });
            }
        }
        done->unlock();
        delete done;
    });
    bool rpcOK;
    auto result = defaultClient->IPTest(&rpcOK, req);
    done->unlock();
    //
    if (!rpcOK || result.results.empty()) return;

    for (const auto &res: result.results) {
        if (!tag2entID.empty()) {
            entID = tag2entID.count(QString::fromStdString(res.outbound_tag.value())) == 0 ? -1 : tag2entID[QString::fromStdString(res.outbound_tag.value())];
        }
        if (entID == -1) {
            MW_show_log(tr("Something is very wrong, the subject ent cannot be found!"));
            continue;
        }

        auto ent = Configs::dataManager->profilesRepo->GetProfile(entID);
        if (ent == nullptr) {
            MW_show_log(tr("Profile manager data is corrupted, try again."));
            continue;
        }

        if (res.error.value().empty()) {

            ent->SetIpTestResult(
                QString::fromStdString(
                    res.ip.value()
                ),

                QString::fromStdString(
                    res.country_code.value()
                )
            );

        }
        else {

            const QString error =
                QString::fromStdString(
                    res.error.value()
                );


            if (!error.contains("test aborted") &&
                !error.contains("context canceled"))
            {
                MW_show_log(
                    tr("[%1] IP test error: %2")
                    .arg(
                        ent->outbound
                        ->DisplayTypeAndName(),
                        error
                    )
                );
            }


            ent->ClearIpTestResult();
        }
        Configs::dataManager->profilesRepo->Save(ent);
    }
}

void MainWindow::urltest_current_group(
    const QList<int>& profileIDs)
{
    if (profileIDs.isEmpty()) {
        return;
    }

    // -------------------------------------------------
    // Global test guard.
    //
    // Atomic test-and-set:
    // false -> true means we successfully acquired it.
    // -------------------------------------------------
    if (speedtestRunning.exchange(
        true,
        std::memory_order_acq_rel))
    {
        MessageBoxWarning(
            software_name,
            tr(
                "The last url test did not exit "
                "completely, please wait. "
                "If it persists, please restart "
                "the program."
            )
        );

        return;
    }

    runOnNewThread(
        [this, profileIDs]()
        {
            // Whatever exit path is taken below,
            // speedtestRunning becomes false.
            AtomicFlagResetGuard runningGuard(
                speedtestRunning
            );

            stopSpeedtest.store(
                false,
                std::memory_order_release
            );

            dataViewHtmlGenerator_
                .seedLatencyTest(
                    DataViewHtmlGenerator::
                    LatencyTestPanelState::
                    Kind::Url,

                    profileIDs.size()
                );
            UpdateDataView(true);

            // =========================================
            // Execute one batch
            // =========================================
            auto speedTestFunc =
                [this](
                    const QList<
                    std::shared_ptr<
                    Configs::Profile
                    >
                    >& profileSlice,

                    const QList<int>& ids)
                {
                    auto buildObject =
                        Configs::BuildTestConfig(
                            profileSlice
                        );

                    if (!buildObject ||
                        !buildObject
                        ->error
                        .isEmpty())
                    {
                        if (buildObject) {

                            MW_show_log(
                                tr(
                                    "Failed to build "
                                    "test config for batch: "
                                )
                                +
                                buildObject->error
                            );
                        }
                        return;
                    }

                    // ---------------------------------
                    // Completion semaphore for THIS
                    // batch only.
                    //
                    // It starts with zero permits.
                    // ---------------------------------
                    auto completion =
                        std::make_shared<
                        QSemaphore
                        >(0);

                    int taskCount = 0;

                    // ---------------------------------
                    // Full standalone configs
                    // ---------------------------------
                    for (const auto entID :
                        buildObject
                        ->fullConfigs
                        .keys())
                    {
                        const QString configStr =
                            buildObject
                            ->fullConfigs[
                                entID
                            ];

                        ++taskCount;

                        parallelCoreCallPool->start(
                            [
                                this,
                                completion,
                                configStr,
                                entID
                            ]()
                            {
                                SemaphoreReleaseGuard
                                    completionGuard(
                                        *completion
                                    );

                                runURLTest(
                                    configStr,
                                    "",
                                    true,
                                    {},
                                    {},
                                    entID
                                );
                            }
                        );
                    }

                    // ---------------------------------
                    // Shared core config
                    // ---------------------------------
                    if (!buildObject
                        ->outboundTags
                        .empty())
                    {
                        ++taskCount;

                        // Keep BuildTestConfig result alive
                        // until worker has finished.
                        auto taskBuildObject =
                            buildObject;

                        parallelCoreCallPool->start(
                            [
                                this,
                                completion,
                                taskBuildObject
                            ]()
                            {
                                SemaphoreReleaseGuard
                                    completionGuard(
                                        *completion
                                    );

                                const QString xrayConf =
                                    taskBuildObject
                                    ->isXrayNeeded
                                    ?
                                    QJsonObject2QString(
                                        taskBuildObject
                                        ->xrayConfig,
                                        false
                                    )
                                    :
                                    "";

                                runURLTest(
                                    QJsonObject2QString(
                                        taskBuildObject
                                        ->coreConfig,
                                        false
                                    ),
                                    xrayConf,
                                    false,
                                    taskBuildObject
                                    ->outboundTags,
                                    taskBuildObject
                                    ->tag2entID
                                );
                            }
                        );
                    }

                    // ---------------------------------
                    // Wait for every task in THIS batch.
                    //
                    // No QMutex ownership tricks.
                    // ---------------------------------
                    if (taskCount > 0) {
                        completion->acquire(
                            taskCount
                        );
                    }

                    MW_show_log(
                        "URL test for batch done."
                    );

                    runOnUiThread(
                        [this, ids]()
                        {
                            refresh_proxy_list(
                                ids
                            );
                        }
                    );
                };

            // =========================================
            // Process profile batches
            // =========================================
            std::shared_ptr<Configs::Group>
                currentGroup;

            for (int i = 0;
                i < profileIDs.size();
                i += 100)
            {
                if (stopSpeedtest.load(
                    std::memory_order_acquire))
                {
                    break;
                }

                const auto profileIDsSlice =
                    profileIDs.mid(
                        i,
                        100
                    );
                const auto profiles =
                    Configs::dataManager
                    ->profilesRepo
                    ->GetProfileBatch(
                        profileIDsSlice
                    );

                if (!currentGroup &&
                    !profiles.isEmpty() &&
                    profiles.first())
                {
                    currentGroup =
                        Configs::dataManager
                        ->groupsRepo
                        ->GetGroup(
                            profiles
                            .first()
                            ->gid
                        );
                }
                speedTestFunc(
                    profiles,
                    profileIDsSlice
                );
            }

            dataViewHtmlGenerator_
                .clearTestSections();

            UpdateDataView(true);

            // =========================================
            // Optional cleanup
            // =========================================
            if (currentGroup) {

                const auto groupSnapshot =
                    currentGroup
                    ->Snapshot();

                if (groupSnapshot
                    .auto_clear_unavailable)
                {
                    MW_show_log(
                        "URL test finished, "
                        "clearing unavailable "
                        "profiles..."
                    );

                    runOnUiThread(
                        [
                            this,
                            profileIDs
                        ]()
                        {
                            clearUnavailableProfiles(
                                false,
                                profileIDs
                            );
                        }
                    );
                }
            }

            MW_show_log(
                tr("URL test finished!")
            );

            // No speedtestRunning.unlock().
            //
            // AtomicFlagResetGuard automatically does:
            //
            // speedtestRunning.store(false)
        }
    );
}

void MainWindow::stopTests() {
    stopSpeedtest.store(true);
    bool ok;
    defaultClient->StopTests(&ok);

    if (!ok) {
        MW_show_log(tr("Failed to stop tests"));
    }
}

void MainWindow::url_test_current() {
    last_test_time = QDateTime::currentSecsSinceEpoch();
    ui->label_running->setText(tr("Testing"));

    runOnNewThread([=,this] {
        libcore::TestReq req;
        req.test_current = true;
        req.url = Configs::dataManager->settingsRepo->test_latency_url.toStdString();

        bool rpcOK;
        auto result = defaultClient->Test(&rpcOK, req);
        if (!rpcOK || result.results.empty()) return;

        auto latency = result.results[0].latency_ms.value();
        last_test_time = QDateTime::currentSecsSinceEpoch();

        runOnUiThread([=,this] {
            if (!result.results[0].error.value().empty()) {
                MW_show_log(QString("UrlTest error: %1").arg(QString::fromStdString(result.results[0].error.value())));
            }
            if (latency <= 0) {
                ui->label_running->setText(tr("Test Result") + ": " + tr("Unavailable"));
            } else if (latency > 0) {
                ui->label_running->setText(tr("Test Result") + ": " + QString("%1 ms").arg(latency));
            }
        });
    });
}

void MainWindow::iptest_current_group(
    const QList<int>& profileIDs)
{
    if (profileIDs.isEmpty()) {
        return;
    }


    if (speedtestRunning.exchange(
        true,
        std::memory_order_acq_rel))
    {
        MessageBoxWarning(
            software_name,
            tr(
                "The last test did not exit "
                "completely, please wait. "
                "If it persists, please restart "
                "the program."
            )
        );

        return;
    }


    runOnNewThread(
        [this, profileIDs]()
        {
            AtomicFlagResetGuard runningGuard(
                speedtestRunning
            );


            stopSpeedtest.store(
                false,
                std::memory_order_release
            );


            dataViewHtmlGenerator_
                .seedLatencyTest(
                    DataViewHtmlGenerator::
                    LatencyTestPanelState::
                    Kind::Ip,

                    profileIDs.size()
                );


            UpdateDataView(true);


            auto ipTestFunc =
                [this](
                    const QList<
                    std::shared_ptr<
                    Configs::Profile
                    >
                    >& profileSlice,

                    const QList<int>& ids)
                {
                    auto buildObject =
                        Configs::BuildTestConfig(
                            profileSlice
                        );


                    if (!buildObject ||
                        !buildObject
                        ->error
                        .isEmpty())
                    {
                        if (buildObject) {

                            MW_show_log(
                                tr(
                                    "Failed to build "
                                    "test config for batch: "
                                )
                                +
                                buildObject->error
                            );
                        }

                        return;
                    }


                    auto completion =
                        std::make_shared<
                        QSemaphore
                        >(0);


                    int taskCount = 0;


                    // ---------------------------------
                    // Standalone configs
                    // ---------------------------------

                    for (const auto entID :
                        buildObject
                        ->fullConfigs
                        .keys())
                    {
                        const QString configStr =
                            buildObject
                            ->fullConfigs[
                                entID
                            ];


                        ++taskCount;


                        parallelCoreCallPool->start(
                            [
                                this,
                                completion,
                                configStr,
                                entID
                            ]()
                            {
                                SemaphoreReleaseGuard
                                    completionGuard(
                                        *completion
                                    );


                                runIPTest(
                                    configStr,
                                    "",
                                    true,
                                    {},
                                    {},
                                    entID
                                );
                            }
                        );
                    }


                    // ---------------------------------
                    // Shared config
                    // ---------------------------------

                    if (!buildObject
                        ->outboundTags
                        .empty())
                    {
                        ++taskCount;


                        auto taskBuildObject =
                            buildObject;


                        parallelCoreCallPool->start(
                            [
                                this,
                                completion,
                                taskBuildObject
                            ]()
                            {
                                SemaphoreReleaseGuard
                                    completionGuard(
                                        *completion
                                    );


                                const QString xrayConf =
                                    taskBuildObject
                                    ->isXrayNeeded
                                    ?
                                    QJsonObject2QString(
                                        taskBuildObject
                                        ->xrayConfig,
                                        false
                                    )
                                    :
                                    "";


                                runIPTest(
                                    QJsonObject2QString(
                                        taskBuildObject
                                        ->coreConfig,
                                        false
                                    ),

                                    xrayConf,

                                    false,

                                    taskBuildObject
                                    ->outboundTags,

                                    taskBuildObject
                                    ->tag2entID
                                );
                            }
                        );
                    }


                    // ---------------------------------
                    // Proper completion barrier
                    // ---------------------------------

                    if (taskCount > 0) {

                        completion->acquire(
                            taskCount
                        );
                    }


                    MW_show_log(
                        "IP test for batch done."
                    );


                    runOnUiThread(
                        [this, ids]()
                        {
                            refresh_proxy_list(
                                ids
                            );
                        }
                    );
                };


            // =========================================
            // Process batches
            // =========================================

            for (int i = 0;
                i < profileIDs.size();
                i += 100)
            {
                if (stopSpeedtest.load(
                    std::memory_order_acquire))
                {
                    break;
                }


                const auto profileIDsSlice =
                    profileIDs.mid(
                        i,
                        100
                    );


                const auto profiles =
                    Configs::dataManager
                    ->profilesRepo
                    ->GetProfileBatch(
                        profileIDsSlice
                    );


                ipTestFunc(
                    profiles,
                    profileIDsSlice
                );
            }


            dataViewHtmlGenerator_
                .clearTestSections();


            UpdateDataView(true);


            MW_show_log(
                tr("IP test finished!")
            );


            // runningGuard resets speedtestRunning.
        }
    );
}
void MainWindow::speedtest_current_group(const QList<int>& profileIDs, bool testCurrent)
{
    if (profileIDs.isEmpty() && !testCurrent) {
        return;
    }
    if (speedtestRunning.exchange(
        true,
        std::memory_order_acq_rel))
    {
        MessageBoxWarning(
            software_name,
            tr(
                "The last speed test did not exit "
                "completely, please wait. "
                "If it persists, please restart "
                "the program."
            )
        );

        return;
    }

    currentUnderTest.store(testCurrent);

    runOnNewThread([this, profileIDs, testCurrent]() {
        AtomicFlagResetGuard runningGuard(
            speedtestRunning
        );

        stopSpeedtest.store(false);
        if (!testCurrent)
        {
            dataViewHtmlGenerator_.seedSpeedTest(profileIDs.size());
            UpdateDataView(true);
            auto speedTestFunc = [=, this](const QList<std::shared_ptr<Configs::Profile>>& profileSlice) {
                auto buildObject = Configs::BuildTestConfig(profileSlice);
                if (!buildObject->error.isEmpty()) {
                    MW_show_log(tr("Failed to build batch test config: ") + buildObject->error);
                    return;
                }

                for (const auto &entID: buildObject->fullConfigs.keys()) {
                    auto configStr = buildObject->fullConfigs[entID];
                    runSpeedTest(configStr, "", true, false, {}, {}, entID);
                }

                if (!buildObject->outboundTags.empty()) {
                    auto xrayConf = buildObject->isXrayNeeded ? QJsonObject2QString(buildObject->xrayConfig, true) : "";
                    runSpeedTest(QJsonObject2QString(buildObject->coreConfig, false), xrayConf, false, false, buildObject->outboundTags, buildObject->tag2entID, -1);
                }
            };
            int stepSize = Configs::dataManager->settingsRepo->speed_test_mode == Configs::TestConfig::COUNTRY ? 100 : 1;
            for (int i=0;i<profileIDs.length();i+=stepSize) {
                if (stopSpeedtest.load()) break;
                auto profileIDsSlice = profileIDs.mid(i, stepSize);
                auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(profileIDsSlice);
                speedTestFunc(profiles);
            }
        } else
        {
            dataViewHtmlGenerator_.seedSpeedTest(1);
            runSpeedTest("", "", true, true, {}, {}, -1);
            currentUnderTest.store(false);
        }
        dataViewHtmlGenerator_.clearTestSections();
        UpdateDataView(true);
        runOnUiThread([=,this]{
            refresh_proxy_list(profileIDs);
            MW_show_log(tr("Speedtest finished!"));
        });
    });
}

void MainWindow::querySpeedtest(const QMap<QString, int>& tag2entID, bool testCurrent)
{
    bool ok;
    auto res = defaultClient->QueryCurrentSpeedTests(&ok);
    if (!ok || !res.is_running.value())
    {
        return;
    }
    auto profile = testCurrent ? running : Configs::dataManager->profilesRepo->GetProfile(tag2entID[QString::fromStdString(res.result.value().outbound_tag.value())]);
    if (profile == nullptr)
    {
        return;
    }
    runOnUiThread([=, this]
    {
        dataViewHtmlGenerator_.setSpeedtestProgress(profile->outbound->name, res.result.value());
        UpdateDataView();

        if (res.result.value().error.value().empty() && !res.result.value().cancelled.value())
        {
            profile->MergeSpeedTestResult(
                QString::fromStdString(
                    res.result.value()
                    .dl_speed.value()
                ),

                QString::fromStdString(
                    res.result.value()
                    .ul_speed.value()
                ),

                res.result.value()
                .latency.value(),

                res.result.value()
                .server_country.value()
                .empty()
                ? QString()
                : CountryNameToCode(
                    QString::fromStdString(
                        res.result.value()
                        .server_country.value()
                    )
                )
            );
            refresh_proxy_list({profile->id});
        }
    });
}

void MainWindow::queryCountryTest(const QMap<QString, int>& tag2entID, bool testCurrent)
{
    bool ok;
    auto res = defaultClient->QueryCountryTestResults(&ok);
    if (!ok || res.results.empty())
    {
        return;
    }
    for (const auto& result : res.results)
    {
        dataViewHtmlGenerator_.addTestProgress();
        UpdateDataView();
        auto profile = testCurrent ? running : Configs::dataManager->profilesRepo->GetProfile(tag2entID[QString::fromStdString(result.outbound_tag.value())]);
        if (profile == nullptr)
        {
            return;
        }
        runOnUiThread([=, this]
        {
            if (result.error.value().empty() && !result.cancelled.value())
            {
                profile->MergeCountryTestResult(
                    result.latency.value(),

                    result.server_country.value()
                    .empty()
                    ? QString()
                    : CountryNameToCode(
                        QString::fromStdString(
                            result.server_country.value()
                        )
                    )
                );
                refresh_proxy_list({profile->id});
            }
        });
    }
    UpdateDataView(true);
}


void MainWindow::runSpeedTest(const QString& config, const QString& xrayConfig, bool useDefault, bool testCurrent, const QStringList& outboundTags, const QMap<QString, int>& tag2entID, int entID)
{
    if (stopSpeedtest.load()) {
        MW_show_log(tr("Profile speed test aborted"));
        return;
    }

    libcore::SpeedTestRequest req;
    auto speedtestConf = Configs::dataManager->settingsRepo->speed_test_mode;
    for (const auto &item: outboundTags) {
        req.outbound_tags.push_back(item.toStdString());
    }
    req.config = config.toStdString();
    req.use_default_outbound = useDefault;
    req.test_download = speedtestConf == Configs::TestConfig::FULL || speedtestConf == Configs::TestConfig::DL;
    req.test_upload = speedtestConf == Configs::TestConfig::FULL || speedtestConf == Configs::TestConfig::UL;
    req.simple_download = speedtestConf == Configs::TestConfig::SIMPLEDL;
    req.simple_download_addr = Configs::dataManager->settingsRepo->simple_dl_url.toStdString();
    req.test_current = testCurrent;
    req.timeout_ms = Configs::dataManager->settingsRepo->speed_test_timeout_ms;
    req.only_country = speedtestConf == Configs::TestConfig::COUNTRY;
    req.country_concurrency = Configs::dataManager->settingsRepo->test_concurrent;
    req.xray_config = xrayConfig.toStdString();
    req.need_xray = !xrayConfig.isEmpty();

    if (speedtestConf != Configs::TestConfig::COUNTRY) {
        dataViewHtmlGenerator_.addTestProgress();
        UpdateDataView();
    }

    // loop query result
    auto doneMu = new QMutex;
    doneMu->lock();
    runOnNewThread([=,this]
    {
        while (true) {
            QThread::msleep(100);
            if (doneMu->tryLock())
            {
                break;
            }
            if (speedtestConf == Configs::TestConfig::COUNTRY)
            {
                queryCountryTest(tag2entID, testCurrent);
            } else
            {
                querySpeedtest(tag2entID, testCurrent);
            }
        }
        doneMu->unlock();
        delete doneMu;
    });
    bool rpcOK;
    auto result = defaultClient->SpeedTest(&rpcOK, req);
    doneMu->unlock();
    //
    if (!rpcOK || result.results.empty()) return;

    for (const auto &res: result.results) {
        if (testCurrent) entID = running ? running->id : -1;
        else {
            entID = tag2entID.count(QString::fromStdString(res.outbound_tag.value())) == 0 ? -1 : tag2entID[QString::fromStdString(res.outbound_tag.value())];
        }
        if (entID == -1) {
            MW_show_log(tr("Something is very wrong, the subject ent cannot be found!"));
            continue;
        }

        auto ent = Configs::dataManager->profilesRepo->GetProfile(entID);
        if (ent == nullptr) {
            MW_show_log(tr("Profile manager data is corrupted, try again."));
            continue;
        }

        if (res.cancelled.value()) continue;

        if (res.error.value().empty()) {

            ent->SetSpeedTestResult(
                QString::fromStdString(
                    res.dl_speed.value()
                ),

                QString::fromStdString(
                    res.ul_speed.value()
                ),

                res.latency.value(),

                res.server_country.value()
                .empty()
                ? QString()
                : CountryNameToCode(
                    QString::fromStdString(
                        res.server_country.value()
                    )
                )
            );
        }

        else {

            ent->SetSpeedTestError();


            MW_show_log(
                tr("[%1] speed test error: %2")
                .arg(
                    ent->outbound
                    ->DisplayTypeAndName(),

                    QString::fromStdString(
                        res.error.value()
                    )
                )
            );
        }
        Configs::dataManager->profilesRepo->Save(ent);
    }
}

bool MainWindow::set_system_dns(bool set, bool save_set) {
    if (!Configs::dataManager->settingsRepo->enable_dns_server) {
        MW_show_log(tr("You need to enable hijack DNS server first"));
        return false;
    }
    if (!get_elevated_permissions(4)) {
        return false;
    }
    bool rpcOK;
    QString res;
    if (set) {
        res = defaultClient->SetSystemDNS(&rpcOK, false);
    } else {
        res = defaultClient->SetSystemDNS(&rpcOK, true);
    }
    if (!rpcOK) {
        MW_show_log(tr("Failed to set system dns: ") + res);
        return false;
    }
    if (save_set) Configs::dataManager->settingsRepo->system_dns_set = set;
    return true;
}

void MainWindow::profile_start(int _id) {
    if (Configs::dataManager->settingsRepo->prepare_exit) return;
#ifdef Q_OS_LINUX
    if (Configs::dataManager->settingsRepo->enable_dns_server && Configs::dataManager->settingsRepo->dns_server_listen_port <= 1024) {
        if (!get_elevated_permissions()) {
            MW_show_log(QString("Failed to get admin access, cannot listen on port %1 without it").arg(Configs::dataManager->settingsRepo->dns_server_listen_port));
            return;
        }
    }
#endif

    auto ents = get_now_selected_list();
    auto ent = (_id < 0 && !ents.isEmpty()) ? Configs::dataManager->profilesRepo->GetProfile(ents.first()) : Configs::dataManager->profilesRepo->GetProfile(_id);
    if (ent == nullptr) return;

    if (select_mode) {
        emit profile_selected(ent->id);
        select_mode = false;
        refresh_status();
        return;
    }

    auto group = Configs::dataManager->groupsRepo->GetGroup(ent->gid);
    if (group == nullptr || group->archive) return;

    auto result = Configs::BuildSingBoxConfig(ent);
    if (!result->error.isEmpty()) {
        MessageBoxWarning(tr("BuildConfig return error"), result->error);
        return;
    }

    auto profile_start_stage2 = [=, this] {
        libcore::LoadConfigReq req;
        req.core_config = QJsonObject2QString(result->coreConfig, true).toStdString();
        req.tun_ipv4_cidr = result->tunIPv4CIDR.toStdString();
        req.disable_stats = Configs::dataManager->settingsRepo->disable_traffic_stats;
        req.xray_config = QJsonObject2QString(result->xrayConfig, true).toStdString();
        req.need_xray = !result->xrayConfig.isEmpty();
        if (!result->extraCoreData->path.isEmpty())
        {
            req.need_extra_process = true;
            req.extra_process_path = result->extraCoreData->path.toStdString();
            req.extra_process_args = result->extraCoreData->args.toStdString();
            req.extra_process_conf = result->extraCoreData->config.toStdString();
            req.extra_no_out = result->extraCoreData->noLog;
        }
        //
        bool rpcOK;
        QString error = defaultClient->Start(&rpcOK, req);
        if (!rpcOK) {
            return false;
        }
        if (!error.isEmpty()) {
            if (error.contains("configure tun interface")) {
                runOnUiThread([=, this] {

                    QMessageBox msg(
                        QMessageBox::Information,
                        tr("Tun device misbehaving"),
                        tr("If you have trouble starting VPN, you can force reset Core process here and then try starting the profile again. The error is %1").arg(error),
                        QMessageBox::NoButton,
                        this
                    );
                    msg.addButton(tr("Reset"), QMessageBox::ActionRole);
                    auto cancel = msg.addButton(tr("Cancel"), QMessageBox::ActionRole);

                    msg.setDefaultButton(cancel);
                    msg.setEscapeButton(cancel);

                    int r = msg.exec() - 2;
                    if (r == 0) {
                        StopVPNProcess();
                    }
                });
                return false;
            }
            runOnUiThread([=] { MessageBoxWarning("LoadConfig return error", error); });
            return false;
        }
        //
        Stats::trafficLooper->SetChainGroups(
            result->chainGroups
        );

        Stats::trafficLooper->loop_enabled.store(
            true,
            std::memory_order_release
        );
        Stats::connection_lister->suspend = false;

        Configs::dataManager->settingsRepo->UpdateStartedId(ent->id);
        running = ent;
        if (Configs::dataManager->settingsRepo->spmode_system_proxy) set_system_proxy(true);

        runOnUiThread([=, this] {
            refresh_status();
            refresh_proxy_list({ent->id});

            auto resp = NetworkRequestHelper::HttpGet("http://ip-api.com/json/", false, true);
            if (resp.error.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(resp.data);
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    QString city = obj["city"].toString();
                    QString countryName = obj["country"].toString();
                    QString countryCode = obj["countryCode"].toString();
                    if (running) running->runningCountryInfo = QString("%1 %2, %3").arg(CountryCodeToFlag(countryCode), countryName, city);
                    refresh_status();
                }
            }
        });

        return true;
    };

    if (!mu_starting.tryLock()) {
        MessageBoxWarning(software_name, tr("Another profile is starting..."));
        return;
    }
    if (!mu_stopping.tryLock()) {
        MessageBoxWarning(software_name, tr("Another profile is stopping..."));
        mu_starting.unlock();
        return;
    }
    mu_stopping.unlock();

    // check core state
    if (!Configs::dataManager->settingsRepo->core_running) {
        runOnThread(
            [=, this] {
                MW_show_log(tr("Try to start the config, but the core has not listened to the RPC port, so restart it..."));
                core_process->start_profile_when_core_is_up = ent->id;
                core_process->Restart();
            },
            DS_cores);
        mu_starting.unlock();
        return; // let CoreProcess call profile_start when core is up
    }

    // timeout message
    auto restartMsgbox = new QMessageBox(QMessageBox::Question, software_name, tr("If there is no response for a long time, it is recommended to restart the software."),
                                         QMessageBox::Yes | QMessageBox::No, this);
    connect(restartMsgbox, &QMessageBox::accepted, this, [=,this] { MW_dialog_message(MwMessage::RestartProgram, {}); });
    auto restartMsgboxTimer = new MessageBoxTimer(this, restartMsgbox, 10000);

    runOnNewThread([=, this] {
        // stop current running
        if (running != nullptr) {
            profile_stop(false, false, true);
            mu_stopping.lock();
            mu_stopping.unlock();
        }
        // do start
        MW_show_log(">>>>>>>> " + tr("Starting profile %1").arg(ent->outbound->DisplayTypeAndName()));
        if (!profile_start_stage2()) {
            MW_show_log("<<<<<<<< " + tr("Failed to start profile %1").arg(ent->outbound->DisplayTypeAndName()));
        }
        mu_starting.unlock();
        // cancel timeout
        runOnUiThread([=] {
            restartMsgboxTimer->cancel();
            restartMsgboxTimer->deleteLater();
            restartMsgbox->deleteLater();
        });
    });
}

void MainWindow::profile_stop(bool crash, bool block, bool manual) {
    if (running == nullptr) {
        return;
    }
    auto id = running->id;

    auto profile_stop_stage2 = [=,this] {
        if (currentUnderTest.load()) {
            bool ok;
            defaultClient->StopTests(&ok);
            if (!ok) MW_show_log("Failed to stop profile tests!");
        }
        if (!crash) {
            bool rpcOK;
            QString error = defaultClient->Stop(&rpcOK);
            if (rpcOK && !error.isEmpty()) {
                runOnUiThread([=,this] { MessageBoxWarning(tr("Stop return error"), error); });
                return false;
            } else if (!rpcOK) {
                return false;
            }
        }
        if (Configs::dataManager->settingsRepo->spmode_system_proxy) set_system_proxy(false);
        return true;
    };

    if (!mu_stopping.tryLock()) {
        return;
    }

    UpdateConnectionListWithRecreate({});

    runOnNewThread([=, this] {
        
        // -------------------------------------------------
        // Final traffic flush
        // -------------------------------------------------

        Stats::trafficLooper
            ->StopAndFlushTraffic();

        Stats::connection_lister->suspend = true;

        // -------------------------------------------------
        // Restart warning
        // -------------------------------------------------

        QMessageBox* restartMsgbox = nullptr;
        MessageBoxTimer* restartMsgboxTimer = nullptr;

        runOnUiThread(
            [this, &restartMsgbox, &restartMsgboxTimer] {

                restartMsgbox = new QMessageBox(
                    QMessageBox::Question,
                    software_name,
                    tr(
                        "If there is no response for a long time, "
                        "it is recommended to restart the software."
                    ),
                    QMessageBox::Yes | QMessageBox::No,
                    this
                );

                connect(
                    restartMsgbox,
                    &QMessageBox::accepted,
                    this,
                    [this] {
                        MW_dialog_message(
                            MwMessage::RestartProgram,
                            {}
                        );
                    }
                );

                restartMsgboxTimer =
                    new MessageBoxTimer(
                        this,
                        restartMsgbox,
                        5000
                    );
            },
            true
        );


        // -------------------------------------------------
        // Stop profile
        // -------------------------------------------------

        MW_show_log(
            ">>>>>>>> "
            + tr("Stopping profile %1")
            .arg(
                running->outbound
                ->DisplayTypeAndName()
            )
        );

        if (!profile_stop_stage2()) {

            MW_show_log(
                "<<<<<<<< "
                + tr(
                    "Failed to stop, "
                    "please restart the program."
                )
            );
        }


        // -------------------------------------------------
        // Persist running state
        // -------------------------------------------------

        if (manual) {
            Configs::dataManager
                ->settingsRepo
                ->UpdateStartedId(-1919);
        }

        running = nullptr;


        // -------------------------------------------------
        // UI cleanup
        // -------------------------------------------------

        runOnUiThread(
            [
                this,
                id,
                &restartMsgboxTimer,
                &restartMsgbox
            ] {

                if (restartMsgboxTimer) {
                    restartMsgboxTimer->cancel();
                    restartMsgboxTimer->deleteLater();
                }

                if (restartMsgbox) {
                    restartMsgbox->deleteLater();
                }

                refresh_status();

                refresh_proxy_list(
                    { id }
                );

                mu_stopping.unlock();
            },
            true
        );

        }, block);
}
