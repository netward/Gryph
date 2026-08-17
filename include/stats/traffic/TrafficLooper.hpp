#pragma once

#include "include/database/entities/Profile.h"
#include "include/configs/generate.h"

#include <atomic>
#include <map>
#include <memory>
#include <string>

#include <QtGlobal>
#include <QString>
#include <QList>
#include <QMutex>

namespace Stats
{

    // =========================================================
    // Aggregate traffic rate used by UI
    // =========================================================

    struct TrafficLooperEntry
    {
        QString tag;

        double downlink_rate = 0;
        double uplink_rate = 0;
    };


    inline QString DisplaySpeed(
        const std::shared_ptr<TrafficLooperEntry>& entry)
    {
        if (!entry)
        {
            return {};
        }


        return
            UNICODE_LRO
            +
            QString("%1↑ %2↓")
            .arg(
                ReadableSize(
                    entry->uplink_rate
                ),

                ReadableSize(
                    entry->downlink_rate
                )
            );
    }


    // =========================================================
    // Runtime chain group
    // =========================================================

    struct TrafficLooperGroup
    {
        QString watchTag;

        QList<
            std::shared_ptr<
            Configs::Profile
            >
        > profiles;


        qint64 last_update = 0;


        double uplink_rate = 0;
        double downlink_rate = 0;
    };


    class TrafficLooper
    {
    public:

        // Changed from outside TrafficLooper worker thread.
        std::atomic_bool
            loop_enabled{ false };


        // Protected by loop_mutex.
        bool looping = false;


        // Main runtime-state mutex.
        QMutex loop_mutex;


        std::shared_ptr<
            TrafficLooperEntry
        > proxy;


        std::shared_ptr<
            TrafficLooperEntry
        > direct;


        // Kept as public API.
        //
        // This implementation is now safe:
        // QueryStats() is performed outside state/persistence locks.
        void UpdateAll();


        void Loop();


        void StopAndFlushTraffic();


        void SetChainGroups(
            const QList<
            Configs::TrafficChainGroup
            >& configGroups
        );


    private:

        // =====================================================
        // Detached RPC result
        //
        // No pointer/reference to TrafficLooper state is stored
        // here. It can therefore safely exist while all
        // TrafficLooper state mutexes are unlocked.
        // =====================================================

        struct TrafficStatsSnapshot
        {
            std::map<
                std::string,
                qint64
            > ups;


            std::map<
                std::string,
                qint64
            > downs;


            qint64 sampledAt = 0;
        };


        // =====================================================
        // RPC phase
        //
        // MUST be called without:
        //
        //   traffic_persistence_mutex
        //   loop_mutex
        //
        // stats_query_mutex may be held.
        // =====================================================

        [[nodiscard]]
        TrafficStatsSnapshot
            FetchStatsSnapshot() const;


        // =====================================================
        // State-application phase
        //
        // Caller MUST hold loop_mutex.
        //
        // NO RPC / network / SQLite operations here.
        // =====================================================

        void ApplyStatsSnapshotLocked(
            const TrafficStatsSnapshot& snapshot
        );


        QList<
            TrafficLooperGroup
        > groups;


        // =====================================================
        // Query serialization
        //
        // QueryStats represents delta-since-last-query.
        //
        // Only one TrafficLooper QueryStats operation should
        // therefore be in flight at a time.
        //
        // IMPORTANT:
        //
        // Never acquire this mutex while holding either:
        //
        //   traffic_persistence_mutex
        //   loop_mutex
        // =====================================================

        QMutex stats_query_mutex;


        // =====================================================
        // Persistence ordering
        //
        // Guarantees that an older periodic DB snapshot cannot
        // be written after the final stop snapshot.
        // =====================================================

        QMutex traffic_persistence_mutex;


        // =====================================================
        // Runtime generation
        //
        // Changed whenever the group/topology state is replaced
        // or final stop is published.
        //
        // A QueryStats response captured for generation N must
        // never be applied to generation N+1.
        //
        // Protected by loop_mutex.
        // =====================================================

        quint64 state_generation_ = 0;


        // Timestamp of last direct traffic sample.
        // Protected by loop_mutex.
        qint64 direct_last_update = 0;


        // Timestamp of last DB traffic persistence.
        // Protected by loop_mutex.
        qint64 lastTrafficSave = 0;


        static constexpr qint64
            TRAFFIC_SAVE_INTERVAL_MS = 5000;
    };


    extern TrafficLooper*
        trafficLooper;

} // namespace Stats