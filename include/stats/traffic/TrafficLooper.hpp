#pragma once

#include <atomic>
#include <memory>

#include <QtGlobal>
#include <QString>
#include <QList>
#include <QMutex>

#include "include/database/entities/Profile.h"
#include "include/configs/generate.h"


namespace Stats {

    // Aggregate rate accumulator used for the status-bar / traffic-graph
    // numbers (one for all proxied traffic combined, one for direct).
    struct TrafficLooperEntry {
        QString tag;

        double downlink_rate = 0;
        double uplink_rate = 0;
    };


    inline QString DisplaySpeed(
        const std::shared_ptr<TrafficLooperEntry>& entry)
    {
        if (!entry) {
            return {};
        }

        return UNICODE_LRO
            + QString("%1↑ %2↓")
            .arg(
                ReadableSize(entry->uplink_rate),
                ReadableSize(entry->downlink_rate)
            );
    }


    // Runtime view of a TrafficChainGroup:
    // same watchTag + profile list, plus bookkeeping
    // for delta-based rate computation.
    struct TrafficLooperGroup {
        QString watchTag;

        QList<std::shared_ptr<Configs::Profile>> profiles;

        qint64 last_update = 0;

        double uplink_rate = 0;
        double downlink_rate = 0;
    };


    class TrafficLooper {
    public:

        // Changed outside TrafficLooper worker thread.
        std::atomic_bool loop_enabled{ false };

        // Current worker state.
        bool looping = false;

        QMutex loop_mutex;


        std::shared_ptr<TrafficLooperEntry> proxy;
        std::shared_ptr<TrafficLooperEntry> direct;


        void UpdateAll();

        void Loop();

        void SetChainGroups(
            const QList<Configs::TrafficChainGroup>& configGroups
        );


    private:

        QList<TrafficLooperGroup> groups;


        // Timestamp of the last direct-traffic sample.
        qint64 direct_last_update = 0;


        // Timestamp of the last persistent traffic save.
        qint64 lastTrafficSave = 0;


        // UI/statistics are refreshed every second,
        // but traffic totals are persisted only every 5 seconds.
        static constexpr qint64
            TRAFFIC_SAVE_INTERVAL_MS = 5000;
    };


    extern TrafficLooper* trafficLooper;

} // namespace Stats
