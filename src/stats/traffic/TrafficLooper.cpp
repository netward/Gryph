#include "include/stats/traffic/TrafficLooper.hpp"

#include "include/api/RPC.h"
#include "include/ui/mainwindow_interface.h"

#include <QThread>
#include <QJsonDocument>
#include <QElapsedTimer>

#include "include/database/ProfilesRepo.h"


namespace Stats {

    TrafficLooper *trafficLooper = new TrafficLooper;
    QElapsedTimer elapsedTimer;

    void TrafficLooper::UpdateAll() {
        if (Configs::dataManager->settingsRepo->disable_traffic_stats) {
            return;
        }

        auto resp = API::defaultClient->QueryStats();
        const auto now = elapsedTimer.elapsed();

        proxy->uplink_rate = 0;
        proxy->downlink_rate = 0;

        // For each chain group, read the matched-outbound's delta-since-last-query
        // and credit it to every user-visible profile in the chain. Aggregate
        // rates from all groups into the proxy entry for the status bar.
        for (auto& group : groups) {
            const auto tagKey = group.watchTag.toStdString();
            if (!resp.ups.contains(tagKey)) continue;
            const auto interval = now - group.last_update;
            group.last_update = now;
            if (interval <= 0) continue;
            const auto up = resp.ups.at(tagKey);
            const auto down = resp.downs.at(tagKey);
            for (auto& profile : group.profiles) {
                profile->traffic_uplink += up;
                profile->traffic_downlink += down;
            }
            group.uplink_rate = static_cast<double>(up) * 1000.0 / static_cast<double>(interval);
            group.downlink_rate = static_cast<double>(down) * 1000.0 / static_cast<double>(interval);
            proxy->uplink_rate += group.uplink_rate;
            proxy->downlink_rate += group.downlink_rate;
        }

        // direct: not part of any chain group, tracked on its own for the
        // status-bar split.
        direct->uplink_rate = 0;
        direct->downlink_rate = 0;
        const std::string directTag = "direct";
        if (resp.ups.contains(directTag)) {
            const auto interval = now - direct_last_update;
            direct_last_update = now;
            if (interval > 0) {
                const auto up = resp.ups.at(directTag);
                const auto down = resp.downs.at(directTag);
                direct->uplink_rate = static_cast<double>(up) * 1000.0 / static_cast<double>(interval);
                direct->downlink_rate = static_cast<double>(down) * 1000.0 / static_cast<double>(interval);
            }
        }
    }

    void TrafficLooper::Loop() {
        elapsedTimer.start();
        while (true) {
            QThread::msleep(1000); // refresh every one second

            if (Configs::dataManager->settingsRepo->disable_traffic_stats) {
                continue;
            }

            // profile start and stop
            if (!loop_enabled) {
                // 停止
                if (looping) {
                    looping = false;
                    runOnUiThread([=] {
                        auto m = GetMainWindow();
                        m->refresh_status("STOP");
                    });
                }
                runOnUiThread([=]
                {
                   auto m = GetMainWindow();
                   m->update_traffic_graph(0, 0, 0, 0);
                });
                continue;
            } else {
                // 开始
                if (!looping) {
                    looping = true;
                }
            }

            // do update
            loop_mutex.lock();

            UpdateAll();

            loop_mutex.unlock();

            // post to UI
            runOnUiThread([=,this] {
                auto m = GetMainWindow();
                if (proxy != nullptr) {
                    m->refresh_status(QObject::tr("Proxy: %1\nDirect: %2").arg(DisplaySpeed(proxy), DisplaySpeed(direct)));
                    m->update_traffic_graph(proxy->downlink_rate, proxy->uplink_rate, direct->downlink_rate, direct->uplink_rate);
                }
                for (const auto& group : groups) {
                    for (const auto& profile : group.profiles) {
                        m->refresh_proxy_list({profile->id});
                        Configs::dataManager->profilesRepo->SaveTraffic(profile);
                    }
                }
            });
        }
    }

    void TrafficLooper::SetChainGroups(const QList<Configs::TrafficChainGroup>& configGroups) {
        proxy = std::make_shared<TrafficLooperEntry>();
        proxy->tag = "proxy";
        direct = std::make_shared<TrafficLooperEntry>();
        direct->tag = "direct";

        // Seed last_update to "now" so the first delta lands against the next
        // tick rather than against time zero — otherwise the first rate sample
        // gets divided by however long the app has been up.
        const auto now = elapsedTimer.isValid() ? elapsedTimer.elapsed() : 0;

        groups.clear();
        for (const auto& configGroup : configGroups) {
            if (configGroup.watchTag.isEmpty() || configGroup.profiles.isEmpty()) continue;
            TrafficLooperGroup g;
            g.watchTag = configGroup.watchTag;
            g.profiles = configGroup.profiles;
            g.last_update = now;
            groups.append(g);
        }
        direct_last_update = now;
    }

} // namespace Stats
