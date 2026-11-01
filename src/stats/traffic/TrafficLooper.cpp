#include "include/stats/traffic/TrafficLooper.hpp"

#include "include/database/ProfilesRepo.h"
#include "include/api/RPC.h"
#include "include/ui/MainWindowAPI.h"

#include <string>
#include <utility>
#include <vector>

#include <QThread>
#include <QElapsedTimer>
#include <QSet>

namespace Stats {

    TrafficLooper* trafficLooper = new TrafficLooper;
    QElapsedTimer elapsedTimer;
    TrafficLooper::TrafficStatsSnapshot
    TrafficLooper::FetchStatsSnapshot() const

    {
        TrafficStatsSnapshot snapshot;

        if (API::defaultClient)
        {
            const auto response = API::defaultClient->QueryStats();

            // Detach RPC/protobuf state from TrafficLooper.
            // The rest of TrafficLooper works only with this ordinary value snapshot.

            for (const auto& [tag, value] : response.ups)
            {
                snapshot.ups.emplace(tag, static_cast<qint64>(value));
            }

            for (const auto& [tag, value] : response.downs)
            {
                snapshot.downs.emplace(tag, static_cast<qint64>(value));
            }
        }

        // Timestamp AFTER QueryStats.
        //
        // This preserves the old interval semantics:
        // RPC duration is part of the sampling interval.
        snapshot.sampledAt = elapsedTimer.isValid()
            ? elapsedTimer.elapsed()
            : 0;

        return snapshot;
    }

    void TrafficLooper::ApplyStatsSnapshotLocked(const TrafficStatsSnapshot& snapshot)
    {
        if (!proxy || !direct)
        {
            return;
        }

        const qint64 now = snapshot.sampledAt;
        proxy->uplink_rate = 0;
        proxy->downlink_rate = 0;

        for (auto& group : groups)
        {
            const std::string tagKey = group.watchTag.toStdString();
            const auto upIt = snapshot.ups.find(tagKey);
            const auto downIt = snapshot.downs.find(tagKey);
            const bool hasUp = upIt != snapshot.ups.end();
            const bool hasDown = downIt != snapshot.downs.end();
            const qint64 interval = now - group.last_update;

            // Advance sample timestamp even when this interval contained no traffic.
            group.last_update = now;
            group.uplink_rate = 0;
            group.downlink_rate = 0;

            if (interval <= 0)
            {
                continue;
            }

            if (!hasUp && !hasDown)
            {
                continue;
            }

            const qint64 up = hasUp
                ? upIt->second
                : 0;

            const qint64 down = hasDown
                ? downIt->second
                : 0;

            for (const auto& profile : group.profiles)
            {
                if (!profile)
                {
                    continue;
                }

                profile->AddTraffic(down, up);
            }

            group.uplink_rate = static_cast<double>(up) * 1000.0 / static_cast<double>(interval);
			group.downlink_rate = static_cast<double>(down) * 1000.0 / static_cast<double>(interval);
            proxy->uplink_rate += group.uplink_rate;
            proxy->downlink_rate += group.downlink_rate;
        }

        direct->uplink_rate = 0;
        direct->downlink_rate = 0;
        static const std::string directTag = "direct";
        const auto directUpIt = snapshot.ups.find(directTag);
        const auto directDownIt = snapshot.downs.find(directTag);
        const bool hasDirectUp = directUpIt != snapshot.ups.end();
        const bool hasDirectDown = directDownIt != snapshot.downs.end();
        const qint64 directInterval = now - direct_last_update;
        direct_last_update = now; 

        if (directInterval <= 0)
        {
            return;
        }

        if (!hasDirectUp && !hasDirectDown)
        {
            return;
        }

        const qint64 directUp = hasDirectUp
            ? directUpIt->second
            : 0;

        const qint64 directDown = hasDirectDown
            ? directDownIt->second
            : 0;

        direct->uplink_rate = static_cast<double>(directUp) * 1000.0 / static_cast<double>(directInterval);
        direct->downlink_rate = static_cast<double>(directDown) * 1000.0 / static_cast<double>(directInterval);
    }

    void TrafficLooper::UpdateAll()
    {
        if (!Configs::dataManager || !Configs::dataManager->settingsRepo)
        {
            return;
        }

        if (Configs::dataManager->settingsRepo->disable_traffic_stats)
        {
            return;
        }

        QMutexLocker queryLocker(&stats_query_mutex);
        quint64 expectedGeneration = 0;
        {
            QMutexLocker stateLocker(&loop_mutex);

            if (!proxy || !direct)
            {
                return;
            }

            expectedGeneration = state_generation_;
        }

        const TrafficStatsSnapshot snapshot = FetchStatsSnapshot();
        QMutexLocker persistenceLocker(&traffic_persistence_mutex);

        {
            QMutexLocker stateLocker(&loop_mutex);

            // Topology changed while QueryStats was running.
            // Never attribute an old response to new Profiles.
            if (state_generation_ != expectedGeneration)
            {
                return;
            }

            ApplyStatsSnapshotLocked(snapshot);
        }
    }

    void TrafficLooper::Loop()
    {
        elapsedTimer.start();

        while (!stopRequested_.load(std::memory_order_acquire))
        {
            QThread::msleep(1000);

            if (!loop_enabled.load(std::memory_order_acquire))
            {
                continue;
            }

            if (!Configs::dataManager || !Configs::dataManager->settingsRepo || !Configs::dataManager->profilesRepo)
            {
                continue;
            }

            if (Configs::dataManager->settingsRepo->disable_traffic_stats)
            {
                continue;
            }

            std::vector<Configs::ProfileTrafficRow> trafficRows;
            QList<int> profileIds;
            QString proxySpeed;
            QString directSpeed;
            double proxyDown = 0;
            double proxyUp = 0;
            double directDown = 0;
            double directUp = 0;
            bool shouldSaveTraffic = false;
            bool staleTick = false;
            quint64 expectedGeneration = 0;
            QMutexLocker queryLocker(&stats_query_mutex);

            {
                QMutexLocker stateLocker(&loop_mutex);

                if (!loop_enabled.load(std::memory_order_acquire))
                {
                    staleTick = true;
                }
                else if (!proxy || !direct)
                {
                    staleTick = true;
                }
                else
                {
                    if (!looping)
                    {
                        looping = true;
                    }

                    expectedGeneration = state_generation_;
                }
            }

            if (staleTick)
            {
                continue;
            }

            const TrafficStatsSnapshot rpcSnapshot = FetchStatsSnapshot();
            QMutexLocker persistenceLocker(&traffic_persistence_mutex);

            {
                QMutexLocker stateLocker(&loop_mutex);

                if (state_generation_ != expectedGeneration)
                {
                    staleTick = true;
                }
                else if (!loop_enabled.load(
                    std::memory_order_acquire))
                {
                    staleTick = true;
                }
                else if (!proxy || !direct)
                {
                    staleTick = true;
                }
                else if (Configs::dataManager->settingsRepo->disable_traffic_stats)
                {
                    staleTick = true;
                }
                else
                {
                    ApplyStatsSnapshotLocked(rpcSnapshot);
                    QSet<int> seenIds;
                    trafficRows.reserve(static_cast<size_t>(groups.size()));
                    
                    for (const auto& group : groups)
                    {
                        for (const auto& profile : group.profiles)
                        {
                            if (!profile)
                            {
                                continue;
                            }

                            const int profileId =  profile->Id();

                            if (profileId < 0)
                            {
                                continue;
                            }

                            if (seenIds.contains(profileId))
                            {
                                continue;
                            }

                            seenIds.insert(profileId);
                            const auto traffic = profile->TrafficSnapshot();
                            Configs::ProfileTrafficRow row;
                            row.id = profileId;
                            row.traffic_dl = static_cast<long long>(traffic.downlink);
                            row.traffic_up = static_cast<long long>(traffic.uplink);
                            trafficRows.push_back(row);
                            profileIds.append(profileId);
                        }
                    }

                    proxySpeed = DisplaySpeed(proxy);
                    proxyDown = proxy->downlink_rate;
                    proxyUp = proxy->uplink_rate;
                    directSpeed = DisplaySpeed(direct);
                    directDown = direct->downlink_rate;
                    directUp = direct->uplink_rate;
                    const qint64 now = elapsedTimer.elapsed();

                    if (!trafficRows.empty() && (now - lastTrafficSave) >= TRAFFIC_SAVE_INTERVAL_MS)
                    {
                        shouldSaveTraffic = true;
                        lastTrafficSave = now;
                    }
                }
            }

            queryLocker.unlock();

            if (staleTick)
            {
                continue;
            }

            if (shouldSaveTraffic && !trafficRows.empty())
            {
                Configs::dataManager->profilesRepo->SaveTrafficBatch(trafficRows);
            }

            persistenceLocker.unlock();

            runOnUiThread(
                [
                    proxySpeed,
                    directSpeed,
                    proxyDown,
                    proxyUp,
                    directDown,
                    directUp,
                    profileIds
                ]()
                {
                    MainWindowApi::RefreshStatus(QObject::tr("Proxy: %1\nDirect: %2").arg(proxySpeed, directSpeed));
                    MainWindowApi::UpdateTrafficGraph(static_cast<int>(proxyDown), static_cast<int>(proxyUp), static_cast<int>(directDown), static_cast<int>(directUp));

                    if (!profileIds.isEmpty())
                    {
                        MainWindowApi::RefreshProxyList(profileIds);
                    }
                }
            );
        }
    }

    void TrafficLooper::StopAndFlushTraffic()
    {
        if (!Configs::dataManager || !Configs::dataManager->settingsRepo || !Configs::dataManager->profilesRepo)
        {
            return;
        }

        std::vector<Configs::ProfileTrafficRow> trafficRows;
        quint64 expectedGeneration = 0;
        bool shouldQuery = false;
        bool stopCommitted = false;
        QMutexLocker queryLocker(&stats_query_mutex);

        {
            QMutexLocker stateLocker(&loop_mutex);
            expectedGeneration = state_generation_;
            shouldQuery = loop_enabled.load(std::memory_order_acquire) && !Configs::dataManager->settingsRepo->disable_traffic_stats && proxy && direct;
        }

        TrafficStatsSnapshot finalRpcSnapshot;

        if (shouldQuery)
        {
            finalRpcSnapshot = FetchStatsSnapshot();
        }
        else
        {
            finalRpcSnapshot.sampledAt = elapsedTimer.isValid()
                ? elapsedTimer.elapsed()
                : 0;
        }

        QMutexLocker persistenceLocker(&traffic_persistence_mutex);

        {
            QMutexLocker stateLocker(&loop_mutex);

            if (state_generation_ != expectedGeneration)
            {
                MW_show_log("TrafficLooper: final traffic snapshot became stale because chain groups changed."
                );
            }
            else
            {
                if (shouldQuery)
                {
                    ApplyStatsSnapshotLocked(finalRpcSnapshot);
                }

                QSet<int> seenIds;

                for (const auto& group : groups)
                {
                    for (const auto& profile : group.profiles)
                    {
                        if (!profile)
                        {
                            continue;
                        }

                        const int profileId = profile->Id();

                        if (profileId < 0)
                        {
                            continue;
                        }

                        if (seenIds.contains(profileId))
                        {
                            continue;
                        }

                        seenIds.insert(profileId);
                        const auto traffic =profile->TrafficSnapshot();
                        Configs::ProfileTrafficRow row;
                        row.id = profileId;
                        row.traffic_dl = static_cast<long long>(traffic.downlink);
                        row.traffic_up = static_cast<long long>(traffic.uplink);
                        trafficRows.push_back(row);
                    }
                }

                loop_enabled.store(false, std::memory_order_release);
                looping = false;
                lastTrafficSave = elapsedTimer.isValid()
                    ? elapsedTimer.elapsed()
                    : 0;

                // Invalidate any TrafficLooper operation which captured the previous runtime generation.
                ++state_generation_;
                stopCommitted = true;
            }
        }

        queryLocker.unlock();

        if (stopCommitted && !trafficRows.empty())
        {
            Configs::dataManager->profilesRepo->SaveTrafficBatch(trafficRows);
        }

        persistenceLocker.unlock();

        if (!stopCommitted)
        {
            return;
        }

        runOnUiThread(
            []()
            {
                MainWindowApi::RefreshStatus("STOP");
                MainWindowApi::UpdateTrafficGraph(0, 0, 0, 0);
            }
        );
    }

    void TrafficLooper::SetChainGroups(const QList<Configs::TrafficChainGroup>& configGroups)
    {
        QMutexLocker stateLocker(&loop_mutex);
        ++state_generation_;
        proxy = std::make_shared<TrafficLooperEntry>();
        proxy->tag = "proxy";
        direct = std::make_shared<TrafficLooperEntry>();
        direct->tag = "direct";

        const qint64 now = elapsedTimer.isValid()
            ? elapsedTimer.elapsed()
            : 0;

        groups.clear();
        groups.reserve(configGroups.size());

        for (const auto& configGroup : configGroups)
        {
            if (configGroup.watchTag.isEmpty() || configGroup.profiles.isEmpty())
            {
                continue;
            }

            TrafficLooperGroup group;
            group.watchTag = configGroup.watchTag;
            group.profiles = configGroup.profiles;
            group.last_update = now;
            groups.append(std::move(group));
        }

        direct_last_update = now;
        lastTrafficSave = now;
    }

    void TrafficLooper::RequestStop() noexcept
    {
        stopRequested_.store(true, std::memory_order_release);
        loop_enabled.store(false, std::memory_order_release);
    }
} // namespace Stats