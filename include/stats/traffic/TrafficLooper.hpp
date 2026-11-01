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
    struct TrafficLooperEntry
    {
        QString tag;

        double downlink_rate = 0;
        double uplink_rate = 0;
    };

    inline QString DisplaySpeed(const std::shared_ptr<TrafficLooperEntry>& entry)
    {
        if (!entry)
        {
            return {};
        }
        return UNICODE_LRO + QString("%1↑ %2↓").arg(ReadableSize(entry->uplink_rate), ReadableSize(entry->downlink_rate));
    }

    struct TrafficLooperGroup
    {
        QString watchTag;

        QList<std::shared_ptr<Configs::Profile>> profiles;

        qint64 last_update = 0;

        double uplink_rate = 0;
        double downlink_rate = 0;
    };

    class TrafficLooper
    {
    public:

        std::atomic_bool loop_enabled{ false };

        bool looping = false;

        QMutex loop_mutex;

        std::shared_ptr<TrafficLooperEntry> proxy;
        std::shared_ptr<TrafficLooperEntry> direct;

        void UpdateAll();

        void Loop();

        void StopAndFlushTraffic();

        void SetChainGroups(const QList<Configs::TrafficChainGroup>& configGroups);

        void RequestStop() noexcept;

    private:
        std::atomic_bool stopRequested_{ false };

        struct TrafficStatsSnapshot
        {
            std::map<std::string, qint64> ups;
            std::map<std::string, qint64> downs;
            qint64 sampledAt = 0;
        };

        [[nodiscard]]
        TrafficStatsSnapshot
            FetchStatsSnapshot() const;

        void ApplyStatsSnapshotLocked(const TrafficStatsSnapshot& snapshot);

        QList<TrafficLooperGroup> groups;

        QMutex stats_query_mutex;

        QMutex traffic_persistence_mutex;

        quint64 state_generation_ = 0;

        qint64 direct_last_update = 0;

        qint64 lastTrafficSave = 0;

        static constexpr qint64 TRAFFIC_SAVE_INTERVAL_MS = 5000;
    };


    extern TrafficLooper*trafficLooper;

} // namespace Stats