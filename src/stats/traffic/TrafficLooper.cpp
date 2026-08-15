#include "include/stats/traffic/TrafficLooper.hpp"

#include "include/api/RPC.h"
#include "include/ui/mainwindowapi.h"

#include <QThread>
#include <QElapsedTimer>
#include <QSet>

#include <string>
#include <utility>

#include "include/database/ProfilesRepo.h"


namespace Stats {

    TrafficLooper* trafficLooper = new TrafficLooper;
    QElapsedTimer elapsedTimer;


    void TrafficLooper::UpdateAll()
    {
        // Traffic statistics are disabled by user settings.
        if (Configs::dataManager
            ->settingsRepo
            ->disable_traffic_stats)
        {
            return;
        }

        // Runtime traffic entries must already be initialized
        // by SetChainGroups().
        if (!proxy || !direct) {
            return;
        }

        // Query traffic deltas from the core.
        auto resp = API::defaultClient->QueryStats();

        const qint64 now =
            elapsedTimer.elapsed();


        // -------------------------------------------------
        // Proxy traffic
        // -------------------------------------------------

        // Aggregate proxy rate is recalculated from scratch
        // on every tick.
        proxy->uplink_rate = 0;
        proxy->downlink_rate = 0;


        for (auto& group : groups) {

            const std::string tagKey =
                group.watchTag.toStdString();


            // Core may theoretically return only one direction,
            // so check upload and download independently.
            const bool hasUp =
                resp.ups.contains(tagKey);

            const bool hasDown =
                resp.downs.contains(tagKey);


            // Calculate interval before replacing last_update.
            const qint64 interval =
                now - group.last_update;

            // Update timestamp on every tick, even when
            // there was no traffic for this tag.
            group.last_update = now;


            // Reset group rate for the current tick.
            group.uplink_rate = 0;
            group.downlink_rate = 0;


            // Invalid/zero interval cannot be used
            // for rate calculation.
            if (interval <= 0) {
                continue;
            }


            // No traffic for this group during this tick.
            if (!hasUp && !hasDown) {
                continue;
            }


            // Missing direction is treated as zero.
            const auto up =
                hasUp
                ? resp.ups.at(tagKey)
                : 0;

            const auto down =
                hasDown
                ? resp.downs.at(tagKey)
                : 0;


            // Credit traffic delta to every user-visible
            // profile participating in this chain.
            for (auto& profile : group.profiles) {

                if (!profile) {
                    continue;
                }

                profile->traffic_uplink += up;
                profile->traffic_downlink += down;
            }


            // Convert delta accumulated during interval
            // to bytes per second.
            group.uplink_rate =
                static_cast<double>(up)
                * 1000.0
                / static_cast<double>(interval);

            group.downlink_rate =
                static_cast<double>(down)
                * 1000.0
                / static_cast<double>(interval);


            // Aggregate all chain groups into the
            // single proxy status entry.
            proxy->uplink_rate +=
                group.uplink_rate;

            proxy->downlink_rate +=
                group.downlink_rate;
        }


        // -------------------------------------------------
        // Direct traffic
        // -------------------------------------------------

        direct->uplink_rate = 0;
        direct->downlink_rate = 0;


        const std::string directTag =
            "direct";


        const bool hasDirectUp =
            resp.ups.contains(directTag);

        const bool hasDirectDown =
            resp.downs.contains(directTag);


        const qint64 directInterval =
            now - direct_last_update;


        // Same principle as chain groups:
        // advance timestamp every tick.
        direct_last_update = now;


        if (directInterval <= 0) {
            return;
        }


        // No direct traffic during this tick.
        if (!hasDirectUp && !hasDirectDown) {
            return;
        }


        const auto directUp =
            hasDirectUp
            ? resp.ups.at(directTag)
            : 0;

        const auto directDown =
            hasDirectDown
            ? resp.downs.at(directTag)
            : 0;


        direct->uplink_rate =
            static_cast<double>(directUp)
            * 1000.0
            / static_cast<double>(directInterval);

        direct->downlink_rate =
            static_cast<double>(directDown)
            * 1000.0
            / static_cast<double>(directInterval);
    }

    void TrafficLooper::Loop()
    {
        elapsedTimer.start();

        while (true) {

            QThread::msleep(1000);


            QList<std::shared_ptr<Configs::Profile>>
                profilesToSave;

            QList<int> profileIds;


            QString proxySpeed;
            QString directSpeed;


            double proxyDown = 0;
            double proxyUp = 0;

            double directDown = 0;
            double directUp = 0;


            bool stopTransition = false;
            bool statisticsUpdated = false;
            bool shouldSaveTraffic = false;
            bool enabled = false;


            {
                QMutexLocker locker(&loop_mutex);


                // -------------------------------------------------
                // Read state while holding the same mutex that
                // protects groups / UpdateAll / SetChainGroups.
                // -------------------------------------------------

                enabled =
                    loop_enabled.load(
                        std::memory_order_acquire
                    );


                // -------------------------------------------------
                // Stopped
                // -------------------------------------------------

                if (!enabled) {

                    // Execute stop transition only once.
                    if (looping) {

                        looping = false;
                        stopTransition = true;


                        QSet<int> seenIds;

                        for (const auto& group : groups) {

                            for (const auto& profile :
                                group.profiles)
                            {
                                if (!profile ||
                                    profile->id < 0)
                                {
                                    continue;
                                }


                                if (seenIds.contains(
                                    profile->id))
                                {
                                    continue;
                                }


                                seenIds.insert(
                                    profile->id
                                );


                                profilesToSave.append(
                                    profile
                                );
                            }
                        }


                        lastTrafficSave =
                            elapsedTimer.elapsed();
                    }
                }

                // -------------------------------------------------
                // Running
                // -------------------------------------------------

                else {

                    if (!looping) {
                        looping = true;
                    }


                    if (!Configs::dataManager
                        ->settingsRepo
                        ->disable_traffic_stats)
                    {
                        // -----------------------------------------
                        // Query and account traffic
                        // -----------------------------------------

                        UpdateAll();


                        // -----------------------------------------
                        // Build one unique profile snapshot
                        // -----------------------------------------

                        QSet<int> seenIds;


                        for (const auto& group : groups) {

                            for (const auto& profile :
                                group.profiles)
                            {
                                if (!profile ||
                                    profile->id < 0)
                                {
                                    continue;
                                }


                                if (seenIds.contains(
                                    profile->id))
                                {
                                    continue;
                                }


                                seenIds.insert(
                                    profile->id
                                );


                                profilesToSave.append(
                                    profile
                                );


                                profileIds.append(
                                    profile->id
                                );
                            }
                        }


                        // -----------------------------------------
                        // UI value snapshot
                        // -----------------------------------------

                        if (proxy) {

                            proxySpeed =
                                DisplaySpeed(proxy);

                            proxyDown =
                                proxy->downlink_rate;

                            proxyUp =
                                proxy->uplink_rate;
                        }


                        if (direct) {

                            directSpeed =
                                DisplaySpeed(direct);

                            directDown =
                                direct->downlink_rate;

                            directUp =
                                direct->uplink_rate;
                        }


                        statisticsUpdated = true;


                        // -----------------------------------------
                        // DB interval
                        // -----------------------------------------

                        const qint64 now =
                            elapsedTimer.elapsed();


                        if (!profilesToSave.isEmpty() &&
                            (now - lastTrafficSave) >=
                            TRAFFIC_SAVE_INTERVAL_MS)
                        {
                            shouldSaveTraffic = true;

                            lastTrafficSave = now;
                        }
                    }
                }
            }


            // -------------------------------------------------
            // Stopped
            // -------------------------------------------------

            if (!enabled) {

                if (stopTransition) {

                    // Final persistence of accumulated traffic.
                    if (!profilesToSave.isEmpty()) {

                        Configs::dataManager
                            ->profilesRepo
                            ->SaveTrafficBatch(
                                profilesToSave
                            );
                    }


                    runOnUiThread([] {

                        MainWindowApi::RefreshStatus(
                            "STOP"
                        );

                        MainWindowApi::UpdateTrafficGraph(
                            0,
                            0,
                            0,
                            0
                        );
                        });
                }


                continue;
            }


            // Traffic statistics are disabled.
            if (!statisticsUpdated) {
                continue;
            }


            // -------------------------------------------------
            // Periodic DB persistence
            // -------------------------------------------------

            if (shouldSaveTraffic) {

                Configs::dataManager
                    ->profilesRepo
                    ->SaveTrafficBatch(
                        profilesToSave
                    );
            }


            // -------------------------------------------------
            // UI update
            // -------------------------------------------------

            runOnUiThread(
                [
                    proxySpeed,
                    directSpeed,
                    proxyDown,
                    proxyUp,
                    directDown,
                    directUp,
                    profileIds
                ]
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


                    if (!profileIds.isEmpty()) {

                        MainWindowApi::RefreshProxyList(
                            profileIds
                        );
                    }
                }
                    );
        }
    }

    void TrafficLooper::SetChainGroups(
        const QList<Configs::TrafficChainGroup>&
        configGroups)
    {
        QMutexLocker locker(&loop_mutex);


        proxy =
            std::make_shared<
            TrafficLooperEntry>();

        proxy->tag = "proxy";


        direct =
            std::make_shared<
            TrafficLooperEntry>();

        direct->tag = "direct";


        // Seed last_update to current time.
        const auto now =
            elapsedTimer.isValid()
            ? elapsedTimer.elapsed()
            : 0;


        groups.clear();


        for (const auto& configGroup :
            configGroups)
        {
            if (
                configGroup.watchTag.isEmpty()
                ||
                configGroup.profiles.isEmpty()
                )
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
                std::move(group)
            );
        }


        direct_last_update = now;
        lastTrafficSave = now;
    }
} // namespace Stats