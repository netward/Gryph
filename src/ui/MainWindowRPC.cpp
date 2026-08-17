#include "include/ui/MainWindow.h"
#include "ui_MainWindow.h"

#include "include/stats/traffic/TrafficLooper.hpp"
#include "include/api/RPC.h"
#include "include/ui/utils//MessageBoxTimer.h"
#include "3rdparty/qv2ray/v2/proxy/QvProxyConfigurator.hpp"

#include <algorithm>
#include <atomic>
#include <memory>

#include <QInputDialog>
#include <QPushButton>
#include <QDesktopServices>
#include <QMessageBox>
#include <QJsonDocument>

#include "include/configs/generate.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"

#include "include/sys/Process.hpp"


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


    [[nodiscard]]
    QString profileDisplayTypeAndName(
        const std::shared_ptr<Configs::Profile>& profile)
    {
        if (!profile)
        {
            return {};
        }

        const auto config =
            profile->ConfigSnapshot();

        return QStringLiteral("[%1] %2")
            .arg(
                config.displayType,
                config.displayName
            );
    }


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

void MainWindow::setup_rpc(QLocalSocket* socket) {
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
    for (const auto& item : outboundTags) {
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
    runOnNewThread([=, this]
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


                    if (!tag2entID.empty())
                    {
                        const QString outboundTag =
                            QString::fromStdString(
                                res.outbound_tag.value()
                            );


                        const auto it =
                            tag2entID.constFind(
                                outboundTag
                            );


                        if (it != tag2entID.constEnd())
                        {
                            entid =
                                it.value();
                        }
                    }


                    if (entid == -1)
                    {
                        continue;
                    }


                    // IMPORTANT:
                    // refresh exactly the profile whose result
                    // was returned by QueryURLTest().
                    profileIDs << entid;


                    auto ent =
                        Configs::dataManager
                        ->profilesRepo
                        ->GetProfile(
                            entid
                        );


                    if (!ent)
                    {
                        continue;
                    }


                    if (res.error.value().empty())
                    {
                        ent->SetLatency(
                            res.latency_ms.value()
                        );
                    }
                    else
                    {
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
                        else
                        {
                            ent->SetLatency(-1);


                            MW_show_log(
                                tr("[%1] test error: %2")
                                .arg(
                                    profileDisplayTypeAndName(
                                        ent
                                    ),
                                    error
                                )
                            );
                        }
                    }


                    Configs::dataManager
                        ->profilesRepo
                        ->Save(ent);


                    needRefresh = true;
                }

                if (needRefresh)
                {
                    UpdateDataView(true);
                    runOnUiThread([=, this] {
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

    for (const auto& res : result.results) {
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
                        profileDisplayTypeAndName(
                            ent
                        ),
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
    for (const auto& item : outboundTags) {
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
    runOnNewThread([=, this]
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
                    }
                    else {
                        if (!QString::fromStdString(res.error.value()).contains("test aborted") &&
                            !QString::fromStdString(res.error.value()).contains("context canceled")) {
                            MW_show_log(tr("[%1] IP test error: %2").arg(profileDisplayTypeAndName(ent), QString::fromStdString(res.error.value())));
                        }
                        ent->ClearIpTestResult();
                    }
                    Configs::dataManager->profilesRepo->Save(ent);
                    needRefresh = true;
                }
                if (needRefresh)
                {
                    UpdateDataView(true);
                    runOnUiThread([=, this] {
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

    for (const auto& res : result.results) {
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
                        profileDisplayTypeAndName(
                            ent
                        ),
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
                            ->GroupId()
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

    runOnNewThread([=, this] {
        libcore::TestReq req;
        req.test_current = true;
        req.url = Configs::dataManager->settingsRepo->test_latency_url.toStdString();

        bool rpcOK;
        auto result = defaultClient->Test(&rpcOK, req);
        if (!rpcOK || result.results.empty()) return;

        auto latency = result.results[0].latency_ms.value();
        last_test_time = QDateTime::currentSecsSinceEpoch();

        runOnUiThread([=, this] {
            if (!result.results[0].error.value().empty()) {
                MW_show_log(QString("UrlTest error: %1").arg(QString::fromStdString(result.results[0].error.value())));
            }
            if (latency <= 0) {
                ui->label_running->setText(tr("Test Result") + ": " + tr("Unavailable"));
            }
            else if (latency > 0) {
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

                for (const auto& entID : buildObject->fullConfigs.keys()) {
                    auto configStr = buildObject->fullConfigs[entID];
                    runSpeedTest(configStr, "", true, false, {}, {}, entID);
                }

                if (!buildObject->outboundTags.empty()) {
                    auto xrayConf = buildObject->isXrayNeeded ? QJsonObject2QString(buildObject->xrayConfig, true) : "";
                    runSpeedTest(QJsonObject2QString(buildObject->coreConfig, false), xrayConf, false, false, buildObject->outboundTags, buildObject->tag2entID, -1);
                }
                };
            int stepSize = Configs::dataManager->settingsRepo->speed_test_mode == Configs::TestConfig::COUNTRY ? 100 : 1;
            for (int i = 0; i < profileIDs.length(); i += stepSize) {
                if (stopSpeedtest.load()) break;
                auto profileIDsSlice = profileIDs.mid(i, stepSize);
                auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(profileIDsSlice);
                speedTestFunc(profiles);
            }
        }
        else
        {
            dataViewHtmlGenerator_.seedSpeedTest(1);
            runSpeedTest("", "", true, true, {}, {}, -1);
            currentUnderTest.store(false);
        }
        dataViewHtmlGenerator_.clearTestSections();
        UpdateDataView(true);
        runOnUiThread([=, this] {
            refresh_proxy_list(profileIDs);
            MW_show_log(tr("Speedtest finished!"));
            });
        });
}

void MainWindow::querySpeedtest(
    const QMap<QString, int>& tag2entID,
    bool testCurrent)
{
    // =====================================================
    // Query current speed-test state from Core
    // =====================================================
    bool ok = false;
    const auto res =
        defaultClient
        ->QueryCurrentSpeedTests(
            &ok
        );
    if (!ok ||
        !res.is_running.value())
    {
        return;
    }

    // =====================================================
    // Validate result
    // =====================================================
    if (!res.result.has_value())
    {
        return;
    }
    const auto& testResult =
        res.result.value();

    // =====================================================
    // Resolve Profile
    //
    // IMPORTANT:
    // Never access the old `running` member directly.
    // =====================================================
    std::shared_ptr<Configs::Profile>
        profile;
    if (testCurrent)
    {
        // -------------------------------------------------
        // Exactly one atomic snapshot for this operation.
        // -------------------------------------------------
        profile =
            runningProfileSnapshot();
    }
    else
    {
        // -------------------------------------------------
        // Resolve Profile from outbound tag
        // -------------------------------------------------
        if (!testResult
            .outbound_tag
            .has_value())
        {
            return;
        }
        const QString outboundTag =
            QString::fromStdString(
                testResult
                .outbound_tag
                .value()
            );

        // Do not use operator[] here.
        //
        // value(..., -1) makes the "tag not found" case
        // explicit and does not depend on a default ID.
        const int profileId =
            tag2entID.value(
                outboundTag,
                -1
            );
        if (profileId < 0)
        {
            return;
        }
        profile =
            Configs::dataManager
            ->profilesRepo
            ->GetProfile(
                profileId
            );
    }
    if (!profile)
    {
        return;
    }

    // =====================================================
    // Take immutable configuration snapshot
    //
    // We only need the profile name for the progress UI.
    // Do not read live outbound directly.
    // =====================================================
    const auto profileConfig =
        profile->ConfigSnapshot();
    const QString profileName =
        profileConfig.name;
    const int profileId =
        profile->Id();

    // =====================================================
    // Update UI and Profile test state
    // =====================================================
    runOnUiThread(
        [
            this,
            profile,
            profileName,
            profileId,
            res
        ]()
        {
            if (!res.result.has_value())
            {
                return;
            }
            const auto& result =
                res.result.value();

            // -------------------------------------------------
            // Speed-test progress
            // -------------------------------------------------
            dataViewHtmlGenerator_
                .setSpeedtestProgress(
                    profileName,
                    result
                );
            UpdateDataView();

            // -------------------------------------------------
            // Successful result
            // -------------------------------------------------
            if (!result
                .error
                .value()
                .empty()
                ||
                result
                .cancelled
                .value())
            {
                return;
            }
            const QString downloadSpeed =
                QString::fromStdString(
                    result
                    .dl_speed
                    .value()
                );
            const QString uploadSpeed =
                QString::fromStdString(
                    result
                    .ul_speed
                    .value()
                );
            const int latency =
                result
                .latency
                .value();
            QString countryCode;
            const std::string serverCountry =
                result
                .server_country
                .value();
            if (!serverCountry.empty())
            {
                countryCode =
                    CountryNameToCode(
                        QString::fromStdString(
                            serverCountry
                        )
                    );
            }

            // -------------------------------------------------
            // Profile owns synchronization of its test state.
            // -------------------------------------------------
            profile->MergeSpeedTestResult(
                downloadSpeed,
                uploadSpeed,
                latency,
                countryCode
            );

            // -------------------------------------------------
            // Refresh exactly this Profile row
            // -------------------------------------------------
            refresh_proxy_list(
                {
                    profileId
                }
            );
        }
    );
}

void MainWindow::queryCountryTest(
    const QMap<QString, int>& tag2entID,
    bool testCurrent)
{
    // =====================================================
    // Query test results from Core
    // =====================================================
    bool ok = false;
    const auto res =
        defaultClient
        ->QueryCountryTestResults(
            &ok
        );
    if (!ok ||
        res.results.empty())
    {
        return;
    }

    // =====================================================
    // Running Profile snapshot
    //
    // For current-profile testing we take exactly ONE
    // snapshot for the entire batch of results.
    // =====================================================
    std::shared_ptr<Configs::Profile>
        currentProfile;
    if (testCurrent)
    {
        currentProfile =
            runningProfileSnapshot();
        if (!currentProfile)
        {
            return;
        }
    }

    // =====================================================
    // Process results
    // =====================================================
    for (const auto& result :
        res.results)
    {
        std::shared_ptr<Configs::Profile>
            profile;

        // -------------------------------------------------
        // Resolve target Profile
        // -------------------------------------------------
        if (testCurrent)
        {
            // Use the stable Profile captured above.
            profile =
                currentProfile;
        }
        else
        {
            // ---------------------------------------------
            // Result must contain outbound tag.
            // ---------------------------------------------
            if (!result
                .outbound_tag
                .has_value())
            {
                continue;
            }
            const QString outboundTag =
                QString::fromStdString(
                    result
                    .outbound_tag
                    .value()
                );

            // ---------------------------------------------
            // Do NOT use:
            //
            // tag2entID[outboundTag]
            //
            // because a missing key may silently produce
            // a default value.
            // ---------------------------------------------
            const int profileId =
                tag2entID.value(
                    outboundTag,
                    -1
                );
            if (profileId < 0)
            {
                continue;
            }
            profile =
                Configs::dataManager
                ->profilesRepo
                ->GetProfile(
                    profileId
                );
        }

        // -------------------------------------------------
        // One missing Profile must not cancel processing
        // of all remaining test results.
        // -------------------------------------------------
        if (!profile)
        {
            continue;
        }
        // Store ID before asynchronous UI callback.
        const int profileId =
            profile->Id();

        // =================================================
        // UI + test-state update
        // =================================================
        runOnUiThread(
            [
                this,
                profile,
                profileId,
                result
            ]()
            {
                // -----------------------------------------
                // Test progress belongs to UI state.
                // -----------------------------------------
                dataViewHtmlGenerator_
                    .addTestProgress();
                UpdateDataView();

                // -----------------------------------------
                // Ignore failed/cancelled result
                // -----------------------------------------
                if (!result
                    .error
                    .value()
                    .empty())
                {
                    return;
                }
                if (result
                    .cancelled
                    .value())
                {
                    return;
                }

                // -----------------------------------------
                // Country
                // -----------------------------------------
                QString countryCode;
                const std::string serverCountry =
                    result
                    .server_country
                    .value();
                if (!serverCountry.empty())
                {
                    countryCode =
                        CountryNameToCode(
                            QString::fromStdString(
                                serverCountry
                            )
                        );
                }

                // -----------------------------------------
                // Update Profile test state
                //
                // Profile owns synchronization internally.
                // -----------------------------------------
                profile
                    ->MergeCountryTestResult(
                        result
                        .latency
                        .value(),

                        countryCode
                    );

                // -----------------------------------------
                // Refresh only affected Profile
                // -----------------------------------------
                refresh_proxy_list(
                    {
                        profileId
                    }
                );
            }
        );
    }

    // =====================================================
    // Final DataView refresh
    // =====================================================
    runOnUiThread(
        [this]()
        {
            UpdateDataView(
                true
            );
        }
    );
}

void MainWindow::runSpeedTest(
    const QString& config,
    const QString& xrayConfig,
    bool useDefault,
    bool testCurrent,
    const QStringList& outboundTags,
    const QMap<QString, int>& tag2entID,
    int entID)
{
    // =====================================================
    // Cancellation
    // =====================================================
    if (stopSpeedtest.load(
        std::memory_order_acquire))
    {
        MW_show_log(
            tr("Profile speed test aborted")
        );
        return;
    }

    // =====================================================
    // Freeze current Profile for the entire test
    //
    // IMPORTANT:
    // If we are testing the currently running profile,
    // take exactly one atomic snapshot now.
    //
    // Do not read global running-profile state again later.
    // =====================================================
    std::shared_ptr<Configs::Profile>
        currentProfile;
    if (testCurrent)
    {
        currentProfile =
            runningProfileSnapshot();
        if (!currentProfile)
        {
            MW_show_log(
                tr(
                    "Cannot start speed test: "
                    "no profile is currently running."
                )
            );
            return;
        }
    }

    // =====================================================
    // Preserve explicitly supplied Profile ID
    //
    // entID is used when runSpeedTest() is called for
    // a single full-config Profile.
    //
    // Never overwrite the original parameter in the result
    // loop.
    // =====================================================
    const int fixedProfileId =
        entID;

    // =====================================================
    // Build request
    // =====================================================
    libcore::SpeedTestRequest req;
    const auto speedtestConf =
        Configs::dataManager
        ->settingsRepo
        ->speed_test_mode;
    for (const auto& item :
        outboundTags)
    {
        req.outbound_tags
            .push_back(
                item.toStdString()
            );
    }

    req.config =
        config.toStdString();
    req.use_default_outbound =
        useDefault;
    req.test_download =
        speedtestConf ==
        Configs::TestConfig::FULL
        ||
        speedtestConf ==
        Configs::TestConfig::DL;
    req.test_upload =
        speedtestConf ==
        Configs::TestConfig::FULL
        ||
        speedtestConf ==
        Configs::TestConfig::UL;
    req.simple_download =
        speedtestConf ==
        Configs::TestConfig::SIMPLEDL;
    req.simple_download_addr =
        Configs::dataManager
        ->settingsRepo
        ->simple_dl_url
        .toStdString();
    req.test_current =
        testCurrent;
    req.timeout_ms =
        Configs::dataManager
        ->settingsRepo
        ->speed_test_timeout_ms;
    req.only_country =
        speedtestConf ==
        Configs::TestConfig::COUNTRY;
    req.country_concurrency =
        Configs::dataManager
        ->settingsRepo
        ->test_concurrent;
    req.xray_config =
        xrayConfig.toStdString();
    req.need_xray =
        !xrayConfig.isEmpty();

    // =====================================================
    // Initial UI progress
    //
    // runSpeedTest() is normally called from the speed-test
    // worker, so UI state must be changed on the UI thread.
    // =====================================================
    if (speedtestConf !=
        Configs::TestConfig::COUNTRY)
    {
        runOnUiThread(
            [this]()
            {
                dataViewHtmlGenerator_
                    .addTestProgress();


                UpdateDataView();
            }
        );
    }

    // =====================================================
    // Background progress polling
    //
    // Old implementation allocated QMutex manually:
    //
    //     new QMutex
    //     delete doneMu
    //
    // A shared atomic flag gives us much simpler lifetime
    // management.
    // =====================================================
    auto rpcFinished =
        std::make_shared<
        std::atomic_bool
        >(
            false
        );

    runOnNewThread(
        [
            this,
            rpcFinished,
            speedtestConf,
            tag2entID,
            testCurrent
        ]()
        {
            while (!rpcFinished->load(
                std::memory_order_acquire))
            {
                QThread::msleep(
                    100
                );

                // SpeedTest() may have completed while
                // this thread was sleeping.
                if (rpcFinished->load(
                    std::memory_order_acquire))
                {
                    break;
                }
                if (speedtestConf ==
                    Configs::TestConfig::COUNTRY)
                {
                    queryCountryTest(
                        tag2entID,
                        testCurrent
                    );
                }
                else
                {
                    querySpeedtest(
                        tag2entID,
                        testCurrent
                    );
                }
            }
        }
    );

    // =====================================================
    // Blocking RPC
    // =====================================================
    bool rpcOK =
        false;
    const auto result =
        defaultClient
        ->SpeedTest(
            &rpcOK,
            req
        );

    // Signal polling worker to exit.
    rpcFinished->store(
        true,
        std::memory_order_release
    );

    // =====================================================
    // Validate RPC result
    // =====================================================
    if (!rpcOK)
    {
        MW_show_log(
            tr(
                "Speed test RPC failed."
            )
        );
        return;
    }
    if (result.results.empty())
    {
        return;
    }

    // =====================================================
    // Process final results
    // =====================================================
    for (const auto& res :
        result.results)
    {
        std::shared_ptr<Configs::Profile>
            profile;
        int targetProfileId =
            -1;

        // -------------------------------------------------
        // Case 1:
        // Current running Profile
        //
        // Use the Profile captured BEFORE SpeedTest().
        // -------------------------------------------------
        if (testCurrent)
        {
            profile =
                currentProfile;
            if (profile)
            {
                targetProfileId =
                    profile->Id();
            }
        }

        // -------------------------------------------------
        // Case 2:
        // Explicitly supplied Profile ID
        //
        // This is important for calls such as:
        //
        // runSpeedTest(..., entID)
        //
        // for individual full-config profiles.
        // -------------------------------------------------
        else if (fixedProfileId >= 0)
        {
            targetProfileId =
                fixedProfileId;
            profile =
                Configs::dataManager
                ->profilesRepo
                ->GetProfile(
                    targetProfileId
                );
        }

        // -------------------------------------------------
        // Case 3:
        // Batch test — resolve Profile by outbound tag
        // -------------------------------------------------
        else
        {
            if (!res
                .outbound_tag
                .has_value())
            {
                MW_show_log(
                    tr(
                        "Speed test result "
                        "does not contain an outbound tag."
                    )
                );
                continue;
            }
            const QString outboundTag =
                QString::fromStdString(
                    res
                    .outbound_tag
                    .value()
                );
            targetProfileId =
                tag2entID.value(
                    outboundTag,
                    -1
                );
            if (targetProfileId >= 0)
            {
                profile =
                    Configs::dataManager
                    ->profilesRepo
                    ->GetProfile(
                        targetProfileId
                    );
            }
        }

        // =================================================
        // Validate target Profile
        // =================================================
        if (targetProfileId < 0)
        {
            MW_show_log(
                tr(
                    "Something is very wrong, "
                    "the subject ent cannot be found!"
                )
            );

            continue;
        }
        if (!profile)
        {
            MW_show_log(
                tr(
                    "Profile manager data is corrupted, "
                    "try again."
                )
            );
            continue;
        }

        // =================================================
        // Cancelled test
        // =================================================
        if (res
            .cancelled
            .value())
        {
            continue;
        }

        // =================================================
        // Successful result
        // =================================================
        if (res
            .error
            .value()
            .empty())
        {
            QString countryCode;
            const std::string serverCountry =
                res
                .server_country
                .value();
            if (!serverCountry.empty())
            {
                countryCode =
                    CountryNameToCode(
                        QString::fromStdString(
                            serverCountry
                        )
                    );
            }
            profile->SetSpeedTestResult(
                QString::fromStdString(
                    res
                    .dl_speed
                    .value()
                ),
                QString::fromStdString(
                    res
                    .ul_speed
                    .value()
                ),
                res
                .latency
                .value(),
                countryCode
            );
        }

        // =================================================
        // Error
        // =================================================
        else
        {
            profile
                ->SetSpeedTestError();

            // Use immutable config snapshot instead of
            // reading mutable outbound state directly.
            const auto profileConfig =
                profile
                ->ConfigSnapshot();
            const QString displayTypeAndName =
                QString(
                    "[%1] %2"
                )
                .arg(
                    profileConfig.displayType,
                    profileConfig.displayName
                );
            MW_show_log(
                tr(
                    "[%1] speed test error: %2"
                )
                .arg(
                    displayTypeAndName,

                    QString::fromStdString(
                        res
                        .error
                        .value()
                    )
                )
            );
        }

        // =================================================
        // Persist Profile test state
        // =================================================
        Configs::dataManager
            ->profilesRepo
            ->Save(
                profile
            );
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
    }
    else {
        res = defaultClient->SetSystemDNS(&rpcOK, true);
    }
    if (!rpcOK) {
        MW_show_log(tr("Failed to set system dns: ") + res);
        return false;
    }
    if (save_set) Configs::dataManager->settingsRepo->system_dns_set = set;
    return true;
}

void MainWindow::resolveRunningProfileCountryAsync(
    const std::shared_ptr<Configs::Profile>& profile,
    const quint64 sessionGeneration)
{
    if (!profile)
    {
        return;
    }


    // ---------------------------------------------------------
    // IMPORTANT
    //
    // HttpGet() is blocking.
    //
    // Therefore absolutely no network operation is performed
    // on the UI thread.
    // ---------------------------------------------------------

    runOnNewThread(
        [
            this,
            profile,
            sessionGeneration
        ]()
        {
            // -------------------------------------------------
            // Blocking HTTP request.
            //
            // We are on a worker thread here.
            // -------------------------------------------------

            const auto response =
                NetworkRequestHelper::HttpGet(
                    "http://ip-api.com/json/",
                    false,
                    true
                );


            // -------------------------------------------------
            // Check session BEFORE doing anything with result.
            //
            // The profile could have been:
            //
            //   stopped;
            //   replaced;
            //   restarted;
            //
            // while HttpGet() was waiting.
            // -------------------------------------------------

            const quint64 currentGeneration =
                runningSessionGeneration_.load(
                    std::memory_order_acquire
                );


            if (currentGeneration !=
                sessionGeneration)
            {
                return;
            }


            const auto currentProfile =
                runningProfileSnapshot();


            if (currentProfile != profile)
            {
                return;
            }


            // -------------------------------------------------
            // HTTP error
            // -------------------------------------------------

            if (!response.error.isEmpty())
            {
                MW_show_log(
                    "Failed to get profile country info: "
                    +
                    response.error
                );

                return;
            }


            // -------------------------------------------------
            // Parse JSON
            // -------------------------------------------------

            QJsonParseError parseError;


            const QJsonDocument document =
                QJsonDocument::fromJson(
                    response.data,
                    &parseError
                );


            if (parseError.error !=
                QJsonParseError::NoError)
            {
                MW_show_log(
                    "Failed to parse profile country info: "
                    +
                    parseError.errorString()
                );

                return;
            }


            if (!document.isObject())
            {
                MW_show_log(
                    "Failed to parse profile country info: "
                    "root JSON value is not an object."
                );

                return;
            }


            const QJsonObject object =
                document.object();


            // -------------------------------------------------
            // Extract location
            // -------------------------------------------------

            const QString city =
                object.value(
                    "city"
                )
                .toString();


            const QString countryName =
                object.value(
                    "country"
                )
                .toString();


            const QString countryCode =
                object.value(
                    "countryCode"
                )
                .toString();


            // -------------------------------------------------
            // Build display string
            //
            // Example:
            //
            // 🇳🇱 Netherlands, Amsterdam
            // -------------------------------------------------

            QStringList locationParts;


            if (!countryName.isEmpty())
            {
                QString countryText =
                    countryName;


                if (!countryCode.isEmpty())
                {
                    const QString flag =
                        CountryCodeToFlag(
                            countryCode
                        );


                    if (!flag.isEmpty())
                    {
                        countryText.prepend(
                            flag
                            +
                            " "
                        );
                    }
                }


                locationParts.append(
                    countryText
                );
            }


            if (!city.isEmpty())
            {
                locationParts.append(
                    city
                );
            }


            const QString countryInfo =
                locationParts.join(
                    ", "
                );


            if (countryInfo.isEmpty())
            {
                return;
            }


            // -------------------------------------------------
            // Check session AGAIN.
            //
            // Although JSON parsing is fast, keeping this check
            // here makes ownership of the async result explicit.
            // -------------------------------------------------

            if (runningSessionGeneration_.load(
                std::memory_order_acquire)
                !=
                sessionGeneration)
            {
                return;
            }


            if (runningProfileSnapshot() !=
                profile)
            {
                return;
            }


            // -------------------------------------------------
            // Thread-safe runtime Profile state
            // -------------------------------------------------

            profile->SetRunningCountryInfo(
                countryInfo
            );


            // -------------------------------------------------
            // UI update
            //
            // Only UI work is posted to the UI thread.
            // -------------------------------------------------

            runOnUiThread(
                [
                    this,
                    profile,
                    sessionGeneration
                ]()
                {
                    // -----------------------------------------
                    // The queued UI callback may execute later.
                    //
                    // Check session one final time.
                    // -----------------------------------------

                    if (runningSessionGeneration_.load(
                        std::memory_order_acquire)
                        !=
                        sessionGeneration)
                    {
                        return;
                    }


                    if (runningProfileSnapshot() !=
                        profile)
                    {
                        return;
                    }


                    refresh_status();
                }
            );
        }
    );
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
        emit profile_selected(ent->Id());
        select_mode = false;
        refresh_status();
        return;
    }

    auto group =
        Configs::dataManager
        ->groupsRepo
        ->GetGroup(
            ent->GroupId()
        );

    if (!group)
    {
        return;
    }

    const auto groupSnapshot =
        group->Snapshot();

    if (groupSnapshot.archive)
    {
        return;
    }

    auto result = Configs::BuildSingBoxConfig(ent);
    if (!result->error.isEmpty()) {
        MessageBoxWarning(tr("BuildConfig return error"), result->error);
        return;
    }

    auto profile_start_stage2 =
        [
            this,
            ent,
            result
        ]() -> bool
        {
            if (!ent ||
                !result)
            {
                return false;
            }


            // =====================================================
            // Build Core request
            // =====================================================

            libcore::LoadConfigReq req;


            req.core_config =
                QJsonObject2QString(
                    result->coreConfig,
                    true
                )
                .toStdString();


            req.tun_ipv4_cidr =
                result
                ->tunIPv4CIDR
                .toStdString();


            req.disable_stats =
                Configs::dataManager
                ->settingsRepo
                ->disable_traffic_stats;


            req.xray_config =
                QJsonObject2QString(
                    result->xrayConfig,
                    true
                )
                .toStdString();


            req.need_xray =
                !result
                ->xrayConfig
                .isEmpty();


            // =====================================================
            // Extra Core
            // =====================================================

            if (result->extraCoreData &&
                !result
                ->extraCoreData
                ->path
                .isEmpty())
            {
                req.need_extra_process =
                    true;


                req.extra_process_path =
                    result
                    ->extraCoreData
                    ->path
                    .toStdString();


                req.extra_process_args =
                    result
                    ->extraCoreData
                    ->args
                    .toStdString();


                req.extra_process_conf =
                    result
                    ->extraCoreData
                    ->config
                    .toStdString();


                req.extra_no_out =
                    result
                    ->extraCoreData
                    ->noLog;
            }


            // =====================================================
            // Start Core
            //
            // This is blocking RPC, but profile_start_stage2 itself
            // is executed by profile_start() on a worker thread.
            // =====================================================

            bool rpcOK =
                false;


            const QString error =
                defaultClient
                ->Start(
                    &rpcOK,
                    req
                );


            // =====================================================
            // RPC transport failure
            //
            // IMPORTANT:
            //
            // Do not publish ANY running state before this check.
            // =====================================================

            if (!rpcOK)
            {
                MW_show_log(
                    "Failed to start profile: "
                    "RPC request failed."
                );

                return false;
            }


            // =====================================================
            // Core returned configuration error
            // =====================================================

            if (!error.isEmpty())
            {
                // -------------------------------------------------
                // Special TUN error
                // -------------------------------------------------

                if (error.contains(
                    "configure tun interface"))
                {
                    runOnUiThread(
                        [
                            this,
                            error
                        ]()
                        {
                            QMessageBox msg(
                                QMessageBox::Information,

                                tr(
                                    "Tun device misbehaving"
                                ),

                                tr(
                                    "If you have trouble starting VPN, "
                                    "you can force reset Core process "
                                    "here and then try starting the "
                                    "profile again. The error is %1"
                                )
                                .arg(
                                    error
                                ),

                                QMessageBox::NoButton,

                                this
                            );


                            msg.addButton(
                                tr("Reset"),
                                QMessageBox::ActionRole
                            );


                            auto* cancel =
                                msg.addButton(
                                    tr("Cancel"),
                                    QMessageBox::ActionRole
                                );


                            msg.setDefaultButton(
                                cancel
                            );


                            msg.setEscapeButton(
                                cancel
                            );


                            const int result =
                                msg.exec()
                                -
                                2;


                            if (result == 0)
                            {
                                StopVPNProcess();
                            }
                        }
                    );


                    return false;
                }


                // -------------------------------------------------
                // Generic Core configuration error
                // -------------------------------------------------

                runOnUiThread(
                    [
                        error
                    ]()
                    {
                        MessageBoxWarning(
                            "LoadConfig return error",
                            error
                        );
                    }
                            );


                return false;
            }


            // =====================================================
            // From this point Core profile is really running.
            //
            // Only NOW publish application runtime state.
            // =====================================================


            // -----------------------------------------------------
            // Traffic groups
            // -----------------------------------------------------

            Stats::trafficLooper
                ->SetChainGroups(
                    result->chainGroups
                );


            Stats::trafficLooper
                ->loop_enabled
                .store(
                    true,
                    std::memory_order_release
                );


            // -----------------------------------------------------
            // Connection lister
            // -----------------------------------------------------

            Stats::connection_lister
                ->suspend =
                false;


            // -----------------------------------------------------
            // Persist running ID
            // -----------------------------------------------------

            Configs::dataManager
                ->settingsRepo
                ->UpdateStartedId(
                    ent->Id()
                );


            // =====================================================
            // Create new runtime session
            //
            // Increment BEFORE asynchronous operations are launched.
            // =====================================================

            const quint64 sessionGeneration =
                runningSessionGeneration_
                .fetch_add(
                    1,
                    std::memory_order_acq_rel
                )
                +
                1;


            // -----------------------------------------------------
            // Runtime country data belongs to this connection
            // session, not permanently to Profile configuration.
            // -----------------------------------------------------

            ent->ClearRunningCountryInfo();


            // -----------------------------------------------------
            // Atomically publish running Profile
            // -----------------------------------------------------

            publishRunningProfile(
                ent
            );


            // -----------------------------------------------------
            // System proxy
            // -----------------------------------------------------

            if (Configs::dataManager
                ->settingsRepo
                ->spmode_system_proxy)
            {
                set_system_proxy(
                    true
                );
            }


            // =====================================================
            // Initial UI update
            // =====================================================

            runOnUiThread(
                [
                    this,
                    ent,
                    sessionGeneration
                ]()
                {
                    // Do not refresh status for a session which
                    // was already replaced before this queued
                    // callback reached the UI thread.

                    if (runningSessionGeneration_.load(
                        std::memory_order_acquire)
                        !=
                        sessionGeneration)
                    {
                        return;
                    }


                    if (runningProfileSnapshot() !=
                        ent)
                    {
                        return;
                    }


                    refresh_status();


                    refresh_proxy_list(
                        {
                            ent->Id()
                        }
                    );
                }
            );


            // =====================================================
            // Public IP/country lookup
            //
            // Fire-and-forget.
            //
            // profile_start_stage2 DOES NOT wait for HTTP.
            // =====================================================

            resolveRunningProfileCountryAsync(
                ent,
                sessionGeneration
            );


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
                core_process->start_profile_when_core_is_up = ent->Id();
                core_process->Restart();
            },
            DS_cores);
        mu_starting.unlock();
        return; // let CoreProcess call profile_start when core is up
    }

    // timeout message
    auto restartMsgbox = new QMessageBox(QMessageBox::Question, software_name, tr("If there is no response for a long time, it is recommended to restart the software."),
        QMessageBox::Yes | QMessageBox::No, this);
    connect(restartMsgbox, &QMessageBox::accepted, this, [=, this] { MW_dialog_message(MwMessage::RestartProgram, {}); });
    auto restartMsgboxTimer = new MessageBoxTimer(this, restartMsgbox, 10000);

    runOnNewThread([=, this] {
        // stop current running
        const auto runningSnapshot =
            runningProfileSnapshot();
        if (runningSnapshot)
        {
            profile_stop(
                false,
                false,
                true
            );
            mu_stopping.lock();
            mu_stopping.unlock();
        }
        // do start
        MW_show_log(">>>>>>>> " + tr("Starting profile %1").arg(profileDisplayTypeAndName(ent)));
        if (!profile_start_stage2()) {
            MW_show_log("<<<<<<<< " + tr("Failed to start profile %1").arg(profileDisplayTypeAndName(ent)));
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

void MainWindow::profile_stop(
    bool crash,
    bool block,
    bool manual)
{
    // =====================================================
    // Freeze the exact Profile being stopped
    //
    // Keep one strong shared_ptr for the whole operation.
    // Never read global running-profile state from the
    // worker again.
    // =====================================================
    const auto stoppingProfile =
        runningProfileSnapshot();
    if (!stoppingProfile)
    {
        return;
    }
    const int stoppingProfileId =
        stoppingProfile->Id();
    const auto stoppingConfig =
        stoppingProfile
        ->ConfigSnapshot();
    const QString stoppingProfileName =
        QString("[%1] %2")
        .arg(
            stoppingConfig.displayType,
            stoppingConfig.displayName
        );

    // =====================================================
    // Core stop procedure
    // =====================================================
    const auto profile_stop_stage2 =
        [
            this,
            crash
        ]() -> bool
        {
            // -------------------------------------------------
            // Stop active tests first
            // -------------------------------------------------
            if (currentUnderTest.load(
                std::memory_order_acquire))
            {
                bool ok =
                    false;
                defaultClient
                    ->StopTests(
                        &ok
                    );
                if (!ok)
                {
                    MW_show_log(
                        "Failed to stop profile tests!"
                    );
                }
            }

            // -------------------------------------------------
            // Stop Core profile
            // -------------------------------------------------
            if (!crash)
            {
                bool rpcOK =
                    false;
                const QString error =
                    defaultClient
                    ->Stop(
                        &rpcOK
                    );
                if (!rpcOK)
                {
                    MW_show_log(
                        "Failed to stop profile: "
                        "RPC request failed."
                    );

                    return false;
                }
                if (!error.isEmpty())
                {
                    runOnUiThread(
                        [
                            this,
                            error
                        ]()
                        {
                            MessageBoxWarning(
                                tr("Stop return error"),
                                error
                            );
                        }
                    );
                    return false;
                }
            }

            // -------------------------------------------------
            // Disable system proxy
            // -------------------------------------------------
            if (Configs::dataManager
                ->settingsRepo
                ->spmode_system_proxy)
            {
                set_system_proxy(
                    false
                );
            }
            return true;
        };

    // =====================================================
    // Only one stop operation at a time
    // =====================================================
    if (!mu_stopping.tryLock())
    {
        return;
    }
    UpdateConnectionListWithRecreate(
        {}
    );

    // =====================================================
    // Stop worker
    // =====================================================
    runOnNewThread(
        [
            this,
            stoppingProfile,
            stoppingProfileId,
            stoppingProfileName,
            profile_stop_stage2,
            manual
        ]()
        {
            // =================================================
            // Final traffic flush
            // =================================================
            Stats::trafficLooper
                ->StopAndFlushTraffic();
            Stats::connection_lister
                ->suspend =
                true;

            // =================================================
            // Restart warning
            // =================================================
            QMessageBox*
                restartMsgbox =
                nullptr;
            MessageBoxTimer*
                restartMsgboxTimer =
                nullptr;

            // Create these objects synchronously on UI thread.
            runOnUiThread(
                [
                    this,
                    &restartMsgbox,
                    &restartMsgboxTimer
                ]()
                {
                    restartMsgbox =
                        new QMessageBox(
                            QMessageBox::Question,
                            software_name,
                            tr(
                                "If there is no response for a long time, "
                                "it is recommended to restart the software."
                            ),
                            QMessageBox::Yes
                            |
                            QMessageBox::No,
                            this
                        );
                    connect(
                        restartMsgbox,
                        &QMessageBox::accepted,
                        this,
                        [this]()
                        {
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

            // =================================================
            // Stop exact captured Profile
            // =================================================
            MW_show_log(
                ">>>>>>>> "
                +
                tr(
                    "Stopping profile %1"
                )
                .arg(
                    stoppingProfileName
                )
            );
            const bool stopped =
                profile_stop_stage2();
            if (!stopped)
            {
                MW_show_log(
                    "<<<<<<<< "
                    +
                    tr(
                        "Failed to stop, "
                        "please restart the program."
                    )
                );
            }
            else
            {
                MW_show_log(
                    "<<<<<<<< "
                    +
                    tr(
                        "Profile %1 stopped"
                    )
                    .arg(
                        stoppingProfileName
                    )
                );
            }

            // =================================================
            // Persist/clear running state
            //
            // Do this only after successful stop.
            // Otherwise Core may still actually be running.
            // =================================================
            if (stopped)
            {
                if (manual)
                {
                    Configs::dataManager
                        ->settingsRepo
                        ->UpdateStartedId(
                            -1919
                        );
                }

                // ---------------------------------------------
                // Clear only if the active Profile is STILL
                // the exact Profile we started stopping.
                //
                // If another Profile was published meanwhile,
                // it must not be cleared by this old worker.
                // ---------------------------------------------
                const bool cleared =
                    clearRunningProfileIf(
                        stoppingProfile
                    );


                if (cleared)
                {
                    // ---------------------------------------------------------
                    // Invalidate all asynchronous operations which belonged
                    // to the stopped runtime session.
                    //
                    // For example an outstanding ip-api HTTP request.
                    // ---------------------------------------------------------

                    runningSessionGeneration_
                        .fetch_add(
                            1,
                            std::memory_order_acq_rel
                        );
                }
                else
                {
                    MW_show_log(
                        "Running profile changed while "
                        "the previous profile was stopping; "
                        "the new active profile was preserved."
                    );
                }
            }

            // =================================================
            // Copy UI object pointers before callback
            //
            // Do not capture the worker's local variables
            // by reference in the cleanup lambda.
            // =================================================
            QMessageBox*
                const restartMsgboxToDelete =
                restartMsgbox;
            MessageBoxTimer*
                const restartTimerToDelete =
                restartMsgboxTimer;

            // =================================================
            // UI cleanup
            // =================================================
            runOnUiThread(
                [
                    this,
                    stoppingProfileId,
                    restartMsgboxToDelete,
                    restartTimerToDelete
                ]()
                {
                    // -----------------------------------------
                    // Restart timer
                    // -----------------------------------------
                    if (restartTimerToDelete)
                    {
                        restartTimerToDelete
                            ->cancel();


                        restartTimerToDelete
                            ->deleteLater();
                    }

                    // -----------------------------------------
                    // Restart dialog
                    // -----------------------------------------
                    if (restartMsgboxToDelete)
                    {
                        restartMsgboxToDelete
                            ->deleteLater();
                    }

                    // -----------------------------------------
                    // Status
                    // -----------------------------------------
                    refresh_status();

                    // -----------------------------------------
                    // Refresh exact stopped Profile row
                    //
                    // THIS is where the old undefined `id`
                    // has been replaced with stoppingProfileId.
                    // -----------------------------------------
                    refresh_proxy_list(
                        {
                            stoppingProfileId
                        }
                    );

                    // -----------------------------------------
                    // Allow next stop operation
                    // -----------------------------------------
                    mu_stopping.unlock();
                },
                true
            );
        },

        block
    );
}