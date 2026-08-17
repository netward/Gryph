#include "include/stats/traffic/TrafficLooper.hpp"

#include "include/api/RPC.h"
#include "include/ui/mainwindowapi.h"

#include <QThread>
#include <QElapsedTimer>
#include <QSet>

#include <string>
#include <utility>
#include <vector>

#include "include/database/ProfilesRepo.h"


namespace Stats {

    TrafficLooper* trafficLooper = new TrafficLooper;
    QElapsedTimer elapsedTimer;

    TrafficLooper::TrafficStatsSnapshot
        TrafficLooper::FetchStatsSnapshot() const
    {
        TrafficStatsSnapshot snapshot;


        // =========================================================
        // IMPORTANT
        //
        // No TrafficLooper state/persistence mutex is held here.
        //
        // QueryStats may wait for IPC response.
        // =========================================================

        if (API::defaultClient)
        {
            const auto response =
                API::defaultClient
                ->QueryStats();


            // Detach RPC/protobuf state from TrafficLooper.
            //
            // The rest of TrafficLooper works only with this
            // ordinary value snapshot.

            for (const auto& [
                tag,
                value
            ] :
                response.ups)
            {
                snapshot.ups.emplace(
                    tag,
                    static_cast<qint64>(
                        value
                        )
                );
            }


            for (const auto& [
                tag,
                value
            ] :
                response.downs)
            {
                snapshot.downs.emplace(
                    tag,
                    static_cast<qint64>(
                        value
                        )
                );
            }
        }


        // Timestamp AFTER QueryStats.
        //
        // This preserves the old interval semantics:
        // RPC duration is part of the sampling interval.
        snapshot.sampledAt =
            elapsedTimer.isValid()
            ? elapsedTimer.elapsed()
            : 0;


        return snapshot;
    }

    void TrafficLooper::ApplyStatsSnapshotLocked(
        const TrafficStatsSnapshot& snapshot)
    {
        // =========================================================
        // PRECONDITION:
        //
        // loop_mutex is already held by caller.
        //
        // There must be:
        //   - no RPC here;
        //   - no SQLite here;
        //   - no long blocking operation here.
        // =========================================================

        if (!proxy ||
            !direct)
        {
            return;
        }


        const qint64 now =
            snapshot.sampledAt;


        // =========================================================
        // Proxy
        // =========================================================

        proxy->uplink_rate =
            0;

        proxy->downlink_rate =
            0;


        for (auto& group :
            groups)
        {
            const std::string tagKey =
                group.watchTag
                .toStdString();


            const auto upIt =
                snapshot.ups.find(
                    tagKey
                );


            const auto downIt =
                snapshot.downs.find(
                    tagKey
                );


            const bool hasUp =
                upIt != snapshot.ups.end();


            const bool hasDown =
                downIt != snapshot.downs.end();


            const qint64 interval =
                now - group.last_update;


            // Advance sample timestamp even when this interval
            // contained no traffic.
            group.last_update =
                now;


            group.uplink_rate =
                0;

            group.downlink_rate =
                0;


            if (interval <= 0)
            {
                continue;
            }


            if (!hasUp &&
                !hasDown)
            {
                continue;
            }


            const qint64 up =
                hasUp
                ? upIt->second
                : 0;


            const qint64 down =
                hasDown
                ? downIt->second
                : 0;


            // -----------------------------------------------------
            // Credit chain traffic to user-visible profiles.
            //
            // Profile::AddTraffic() performs its own traffic-state
            // synchronization.
            // -----------------------------------------------------

            for (const auto& profile :
                group.profiles)
            {
                if (!profile)
                {
                    continue;
                }


                profile->AddTraffic(
                    down,
                    up
                );
            }


            group.uplink_rate =
                static_cast<double>(
                    up
                    )
                *
                1000.0
                /
                static_cast<double>(
                    interval
                    );


            group.downlink_rate =
                static_cast<double>(
                    down
                    )
                *
                1000.0
                /
                static_cast<double>(
                    interval
                    );


            proxy->uplink_rate +=
                group.uplink_rate;


            proxy->downlink_rate +=
                group.downlink_rate;
        }


        // =========================================================
        // Direct
        // =========================================================

        direct->uplink_rate =
            0;

        direct->downlink_rate =
            0;


        static const std::string
            directTag =
            "direct";


        const auto directUpIt =
            snapshot.ups.find(
                directTag
            );


        const auto directDownIt =
            snapshot.downs.find(
                directTag
            );


        const bool hasDirectUp =
            directUpIt !=
            snapshot.ups.end();


        const bool hasDirectDown =
            directDownIt !=
            snapshot.downs.end();


        const qint64 directInterval =
            now
            -
            direct_last_update;


        direct_last_update =
            now;


        if (directInterval <= 0)
        {
            return;
        }


        if (!hasDirectUp &&
            !hasDirectDown)
        {
            return;
        }


        const qint64 directUp =
            hasDirectUp
            ? directUpIt->second
            : 0;


        const qint64 directDown =
            hasDirectDown
            ? directDownIt->second
            : 0;


        direct->uplink_rate =
            static_cast<double>(
                directUp
                )
            *
            1000.0
            /
            static_cast<double>(
                directInterval
                );


        direct->downlink_rate =
            static_cast<double>(
                directDown
                )
            *
            1000.0
            /
            static_cast<double>(
                directInterval
                );
    }

    void TrafficLooper::UpdateAll()
    {
        if (!Configs::dataManager ||
            !Configs::dataManager->settingsRepo)
        {
            return;
        }


        if (Configs::dataManager
            ->settingsRepo
            ->disable_traffic_stats)
        {
            return;
        }


        // =========================================================
        // Serialize only QueryStats operations.
        // =========================================================

        QMutexLocker queryLocker(
            &stats_query_mutex
        );


        quint64 expectedGeneration =
            0;


        // =========================================================
        // Capture state identity quickly.
        //
        // NO RPC while loop_mutex is held.
        // =========================================================

        {
            QMutexLocker stateLocker(
                &loop_mutex
            );


            if (!proxy ||
                !direct)
            {
                return;
            }


            expectedGeneration =
                state_generation_;
        }


        // =========================================================
        // RPC
        //
        // ONLY stats_query_mutex is held here.
        // =========================================================

        const TrafficStatsSnapshot snapshot =
            FetchStatsSnapshot();


        // =========================================================
        // Apply
        //
        // Keep normal ordering with persistence/final flush.
        // =========================================================

        QMutexLocker persistenceLocker(
            &traffic_persistence_mutex
        );


        {
            QMutexLocker stateLocker(
                &loop_mutex
            );


            // Topology changed while QueryStats was running.
            //
            // Never attribute an old response to new Profiles.
            if (state_generation_ !=
                expectedGeneration)
            {
                return;
            }


            ApplyStatsSnapshotLocked(
                snapshot
            );
        }
    }

    void TrafficLooper::Loop()
    {
        elapsedTimer.start();


        while (true)
        {
            // Statistics/UI update frequency.
            QThread::msleep(
                1000
            );


            // =====================================================
            // Quick checks before entering query pipeline
            // =====================================================

            if (!loop_enabled.load(
                std::memory_order_acquire))
            {
                continue;
            }


            if (!Configs::dataManager ||
                !Configs::dataManager->settingsRepo ||
                !Configs::dataManager->profilesRepo)
            {
                continue;
            }


            if (Configs::dataManager
                ->settingsRepo
                ->disable_traffic_stats)
            {
                continue;
            }


            // =====================================================
            // Immutable output of this tick
            // =====================================================

            std::vector<
                Configs::ProfileTrafficRow
            > trafficRows;


            QList<int>
                profileIds;


            QString proxySpeed;
            QString directSpeed;


            double proxyDown = 0;
            double proxyUp = 0;

            double directDown = 0;
            double directUp = 0;


            bool shouldSaveTraffic =
                false;


            bool staleTick =
                false;


            quint64 expectedGeneration =
                0;


            // =====================================================
            // QUERY SERIALIZATION
            //
            // This prevents periodic QueryStats and final
            // QueryStats from draining the same delta stream
            // concurrently.
            //
            // IMPORTANT:
            //
            // At this moment neither loop_mutex nor
            // traffic_persistence_mutex is held.
            // =====================================================

            QMutexLocker queryLocker(
                &stats_query_mutex
            );


            // =====================================================
            // Capture generation/state
            //
            // Very short lock.
            // =====================================================

            {
                QMutexLocker stateLocker(
                    &loop_mutex
                );


                if (!loop_enabled.load(
                    std::memory_order_acquire))
                {
                    staleTick =
                        true;
                }


                else if (!proxy ||
                    !direct)
                {
                    staleTick =
                        true;
                }


                else
                {
                    if (!looping)
                    {
                        looping =
                            true;
                    }


                    expectedGeneration =
                        state_generation_;
                }
            }


            if (staleTick)
            {
                continue;
            }


            // =====================================================
            // NETWORK / RPC PHASE
            //
            // NO:
            //   loop_mutex
            //   traffic_persistence_mutex
            //
            // SetChainGroups() remains free to run while the
            // RPC request is waiting for the core.
            // =====================================================

            const TrafficStatsSnapshot rpcSnapshot =
                FetchStatsSnapshot();


            // =====================================================
            // PERSISTENCE ORDERING
            //
            // We acquire this only AFTER RPC has completed.
            // =====================================================

            QMutexLocker persistenceLocker(
                &traffic_persistence_mutex
            );


            // =====================================================
            // APPLY + FREEZE
            // =====================================================

            {
                QMutexLocker stateLocker(
                    &loop_mutex
                );


                // -------------------------------------------------
                // Check whether the snapshot still belongs to the
                // current TrafficLooper topology.
                // -------------------------------------------------

                if (state_generation_ !=
                    expectedGeneration)
                {
                    staleTick =
                        true;
                }


                else if (!loop_enabled.load(
                    std::memory_order_acquire))
                {
                    staleTick =
                        true;
                }


                else if (!proxy ||
                    !direct)
                {
                    staleTick =
                        true;
                }


                else if (Configs::dataManager
                    ->settingsRepo
                    ->disable_traffic_stats)
                {
                    staleTick =
                        true;
                }


                else
                {
                    // =============================================
                    // Apply RPC snapshot
                    // =============================================

                    ApplyStatsSnapshotLocked(
                        rpcSnapshot
                    );


                    // =============================================
                    // Build immutable DB snapshot
                    // =============================================

                    QSet<int>
                        seenIds;


                    trafficRows.reserve(
                        static_cast<size_t>(
                            groups.size()
                            )
                    );


                    for (const auto& group :
                        groups)
                    {
                        for (const auto& profile :
                            group.profiles)
                        {
                            if (!profile)
                            {
                                continue;
                            }


                            const int profileId =
                                profile->Id();


                            if (profileId < 0)
                            {
                                continue;
                            }


                            if (seenIds.contains(
                                profileId))
                            {
                                continue;
                            }


                            seenIds.insert(
                                profileId
                            );


                            const auto traffic =
                                profile
                                ->TrafficSnapshot();


                            Configs::ProfileTrafficRow row;


                            row.id =
                                profileId;


                            row.traffic_dl =
                                static_cast<long long>(
                                    traffic.downlink
                                    );


                            row.traffic_up =
                                static_cast<long long>(
                                    traffic.uplink
                                    );


                            trafficRows.push_back(
                                row
                            );


                            profileIds.append(
                                profileId
                            );
                        }
                    }


                    // =============================================
                    // UI snapshot
                    // =============================================

                    proxySpeed =
                        DisplaySpeed(
                            proxy
                        );


                    proxyDown =
                        proxy
                        ->downlink_rate;


                    proxyUp =
                        proxy
                        ->uplink_rate;


                    directSpeed =
                        DisplaySpeed(
                            direct
                        );


                    directDown =
                        direct
                        ->downlink_rate;


                    directUp =
                        direct
                        ->uplink_rate;


                    // =============================================
                    // Persistence interval
                    // =============================================

                    const qint64 now =
                        elapsedTimer.elapsed();


                    if (!trafficRows.empty() &&
                        (now - lastTrafficSave) >=
                        TRAFFIC_SAVE_INTERVAL_MS)
                    {
                        shouldSaveTraffic =
                            true;


                        lastTrafficSave =
                            now;
                    }
                }
            }


            // =====================================================
            // Query response has now been APPLIED or rejected.
            //
            // It is safe to allow StopAndFlushTraffic() to issue
            // the next QueryStats.
            //
            // Keep persistenceLocker for a little longer so the
            // database ordering is still preserved.
            // =====================================================

            queryLocker.unlock();


            if (staleTick)
            {
                continue;
            }


            // =====================================================
            // SQLite persistence
            //
            // loop_mutex is NOT held.
            //
            // traffic_persistence_mutex remains held so a final
            // snapshot can never be persisted before this older
            // periodic snapshot.
            // =====================================================

            if (shouldSaveTraffic &&
                !trafficRows.empty())
            {
                Configs::dataManager
                    ->profilesRepo
                    ->SaveTrafficBatch(
                        trafficRows
                    );
            }


            // =====================================================
            // Release persistence lock BEFORE UI work.
            // =====================================================

            persistenceLocker.unlock();


            // =====================================================
            // UI
            // =====================================================

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
                    MainWindowApi::RefreshStatus(
                        QObject::tr(
                            "Proxy: %1\nDirect: %2"
                        )
                        .arg(
                            proxySpeed,
                            directSpeed
                        )
                    );


                    MainWindowApi::UpdateTrafficGraph(
                        static_cast<int>(
                            proxyDown
                            ),

                        static_cast<int>(
                            proxyUp
                            ),

                        static_cast<int>(
                            directDown
                            ),

                        static_cast<int>(
                            directUp
                            )
                    );


                    if (!profileIds.isEmpty())
                    {
                        MainWindowApi::
                            RefreshProxyList(
                                profileIds
                            );
                    }
                }
            );
        }
    }

    void TrafficLooper::StopAndFlushTraffic()
    {
        if (!Configs::dataManager ||
            !Configs::dataManager->settingsRepo ||
            !Configs::dataManager->profilesRepo)
        {
            return;
        }


        std::vector<
            Configs::ProfileTrafficRow
        > trafficRows;


        quint64 expectedGeneration =
            0;


        bool shouldQuery =
            false;


        bool stopCommitted =
            false;


        // =========================================================
        // Serialize against periodic QueryStats.
        //
        // This is NOT loop_mutex and NOT persistence mutex.
        //
        // SetChainGroups() therefore does not block on the RPC.
        // =========================================================

        QMutexLocker queryLocker(
            &stats_query_mutex
        );


        // =========================================================
        // Capture state before final RPC
        // =========================================================

        {
            QMutexLocker stateLocker(
                &loop_mutex
            );


            expectedGeneration =
                state_generation_;


            shouldQuery =
                loop_enabled.load(
                    std::memory_order_acquire
                )
                &&
                !Configs::dataManager
                ->settingsRepo
                ->disable_traffic_stats
                &&
                proxy
                &&
                direct;
        }


        // =========================================================
        // FINAL RPC
        //
        // Core is still alive at this point.
        //
        // Crucially:
        //
        // traffic_persistence_mutex = FREE
        // loop_mutex                = FREE
        //
        // Only stats_query_mutex is held.
        // =========================================================

        TrafficStatsSnapshot finalRpcSnapshot;


        if (shouldQuery)
        {
            finalRpcSnapshot =
                FetchStatsSnapshot();
        }
        else
        {
            finalRpcSnapshot.sampledAt =
                elapsedTimer.isValid()
                ? elapsedTimer.elapsed()
                : 0;
        }


        // =========================================================
        // Serialize final DB snapshot against older periodic save
        // =========================================================

        QMutexLocker persistenceLocker(
            &traffic_persistence_mutex
        );


        {
            QMutexLocker stateLocker(
                &loop_mutex
            );


            // -----------------------------------------------------
            // If SetChainGroups() replaced the topology while the
            // final RPC was in flight, this response belongs to
            // another generation.
            //
            // Do NOT apply it to new Profiles and do NOT stop the
            // new TrafficLooper generation.
            // -----------------------------------------------------

            if (state_generation_ !=
                expectedGeneration)
            {
                MW_show_log(
                    "TrafficLooper: final traffic snapshot "
                    "became stale because chain groups changed."
                );
            }


            else
            {
                // =================================================
                // Apply final core delta
                // =================================================

                if (shouldQuery)
                {
                    ApplyStatsSnapshotLocked(
                        finalRpcSnapshot
                    );
                }


                // =================================================
                // Freeze final cumulative traffic counters
                // =================================================

                QSet<int>
                    seenIds;


                for (const auto& group :
                    groups)
                {
                    for (const auto& profile :
                        group.profiles)
                    {
                        if (!profile)
                        {
                            continue;
                        }


                        const int profileId =
                            profile->Id();


                        if (profileId < 0)
                        {
                            continue;
                        }


                        if (seenIds.contains(
                            profileId))
                        {
                            continue;
                        }


                        seenIds.insert(
                            profileId
                        );


                        const auto traffic =
                            profile
                            ->TrafficSnapshot();


                        Configs::ProfileTrafficRow row;


                        row.id =
                            profileId;


                        row.traffic_dl =
                            static_cast<long long>(
                                traffic.downlink
                                );


                        row.traffic_up =
                            static_cast<long long>(
                                traffic.uplink
                                );


                        trafficRows.push_back(
                            row
                        );
                    }
                }


                // =================================================
                // Publish STOP
                // =================================================

                loop_enabled.store(
                    false,
                    std::memory_order_release
                );


                looping =
                    false;


                lastTrafficSave =
                    elapsedTimer.isValid()
                    ? elapsedTimer.elapsed()
                    : 0;


                // Invalidate any TrafficLooper operation which
                // captured the previous runtime generation.
                ++state_generation_;


                stopCommitted =
                    true;
            }
        }


        // =========================================================
        // Final response has been applied.
        //
        // The next QueryStats operation may now begin.
        //
        // We intentionally keep persistenceLocker until final
        // traffic has been written.
        // =========================================================

        queryLocker.unlock();


        // =========================================================
        // Final SQLite persistence
        //
        // NO loop_mutex.
        // =========================================================

        if (stopCommitted &&
            !trafficRows.empty())
        {
            Configs::dataManager
                ->profilesRepo
                ->SaveTrafficBatch(
                    trafficRows
                );
        }


        persistenceLocker.unlock();


        // =========================================================
        // Do not let an old stop operation overwrite UI belonging
        // to a newly-installed generation.
        // =========================================================

        if (!stopCommitted)
        {
            return;
        }


        // =========================================================
        // Final UI
        // =========================================================

        runOnUiThread(
            []()
            {
                MainWindowApi::RefreshStatus(
                    "STOP"
                );


                MainWindowApi::UpdateTrafficGraph(
                    0,
                    0,
                    0,
                    0
                );
            }
        );
    }

    void TrafficLooper::SetChainGroups(
        const QList<
        Configs::TrafficChainGroup
        >& configGroups)
    {
        QMutexLocker stateLocker(
            &loop_mutex
        );


        // =========================================================
        // New topology generation
        //
        // Any QueryStats response which started before this point
        // must not be applied to these new groups.
        // =========================================================

        ++state_generation_;


        proxy =
            std::make_shared<
            TrafficLooperEntry
            >();


        proxy->tag =
            "proxy";


        direct =
            std::make_shared<
            TrafficLooperEntry
            >();


        direct->tag =
            "direct";


        const qint64 now =
            elapsedTimer.isValid()
            ? elapsedTimer.elapsed()
            : 0;


        groups.clear();


        groups.reserve(
            configGroups.size()
        );


        for (const auto& configGroup :
            configGroups)
        {
            if (configGroup.watchTag.isEmpty() ||
                configGroup.profiles.isEmpty())
            {
                continue;
            }


            TrafficLooperGroup group;


            group.watchTag =
                configGroup.watchTag;


            group.profiles =
                configGroup.profiles;


            group.last_update =
                now;


            groups.append(
                std::move(
                    group
                )
            );
        }


        direct_last_update =
            now;


        lastTrafficSave =
            now;
    }
} // namespace Stats