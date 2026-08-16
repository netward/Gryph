#include <include/database/entities/Group.h>

#include "include/database/ProfilesRepo.h"
#include "include/global/Configs.hpp"

namespace Configs
{
    void Group::clearCalculatedColumnWidth()
    {
        QMutexLocker locker(&mutex);

        calculated_column_width.clear();
    }

    GroupSnapshot Group::Snapshot() const
    {
        QMutexLocker locker(&mutex);


        GroupSnapshot snapshot;


        snapshot.id =
            id;


        snapshot.archive =
            archive;

        snapshot.skip_auto_update =
            skip_auto_update;

        snapshot.auto_clear_unavailable =
            auto_clear_unavailable;


        snapshot.name =
            name;

        snapshot.url =
            url;

        snapshot.info =
            info;


        snapshot.sub_last_update =
            sub_last_update;


        snapshot.front_proxy_id =
            front_proxy_id;

        snapshot.landing_proxy_id =
            landing_proxy_id;


        snapshot.column_width =
            column_width;

        snapshot.profiles =
            profiles;


        snapshot.scroll_last_profile =
            scroll_last_profile;


        snapshot.test_sort_by =
            test_sort_by;

        snapshot.traffic_sort_by =
            traffic_sort_by;

        snapshot.test_items_to_show =
            test_items_to_show;


        return snapshot;
    }

    QList<int> Group::Profiles() const
    {
        QMutexLocker locker(&mutex);

        return profiles;
    }

    double bitrateToBps(const QString& str)
    {
        if (str.endsWith("Gbps", Qt::CaseInsensitive)) {
            double val = str.left(str.size() - 4).toDouble();
            return val * 1e9;
        }
        if (str.endsWith("Mbps", Qt::CaseInsensitive)) {
            double val = str.left(str.size() - 4).toDouble();
            return val * 1e6;
        }
        if (str.endsWith("Kbps", Qt::CaseInsensitive)) {
            double val = str.left(str.size() - 4).toDouble();
            return val * 1e3;
        }
        if (str == "N/A") return -1;
        return 0.0;
    }

    bool Group::SortProfiles(
        GroupSortAction sortAction)
    {
        QList<int> idsSnapshot;

        // -------------------------------------------------
        // Phase 1:
        // snapshot Group state without touching ProfilesRepo
        // -------------------------------------------------

        {
            QMutexLocker locker(&mutex);

            idsSnapshot = profiles;
        }

        // No profile data is necessary for these modes.
        if (sortAction.method == GroupSortMethod::Raw) {
            return true;
        }

        if (sortAction.method == GroupSortMethod::ById) {

            QMutexLocker locker(&mutex);

            // Do not overwrite concurrent changes.
            if (profiles != idsSnapshot) {
                return false;
            }

            std::ranges::sort(
                profiles,
                [descending = sortAction.descending](
                    const int a,
                    const int b)
                {
                    return descending
                        ? a > b
                        : a < b;
                }
            );

            return true;
        }

        // -------------------------------------------------
        // Phase 2:
        // access ProfilesRepo WITHOUT Group::mutex
        // -------------------------------------------------

        const auto loadedProfiles =
            dataManager
            ->profilesRepo
            ->GetProfileBatch(
                idsSnapshot
            );

        // Build local lookup.
        //
        // After this point SortProfiles does not need to
        // call ProfilesRepo while holding Group::mutex.
        QHash<
            int,
            std::shared_ptr<Profile>
        > profileById;


        profileById.reserve(
            loadedProfiles.size()
        );

        for (const auto& profile :
            loadedProfiles)
        {
            if (!profile ||
                profile->id < 0)
            {
                continue;
            }


            profileById.insert(
                profile->id,
                profile
            );
        }

        // -------------------------------------------------
        // Phase 3:
        // lock Group only after ProfilesRepo has finished
        // -------------------------------------------------

        QMutexLocker locker(&mutex);

        // The group may have changed while ProfilesRepo
        // was loading profiles.
        //
        // Never sort a stale snapshot over newer state.
        if (profiles != idsSnapshot) {
            return false;
        }

        const auto getProfile =
            [&profileById](int id)
            -> std::shared_ptr<Profile>
            {
                const auto it =
                    profileById.constFind(id);

                if (it == profileById.constEnd()) {
                    return nullptr;
                }

                return it.value();
            };

        const auto latencyForSort =
            [](const std::shared_ptr<Profile>& profile)
            {
                if (!profile) {
                    return 100000;
                }

                int value = profile->latency;

                if (value == 0) {
                    value = 100000;
                }

                if (value < 0) {
                    value = 99999;
                }

                return value;
            };

        std::ranges::sort(
            profiles,
            [&](const int a, const int b)
            {
                const auto profA =
                    getProfile(a);

                const auto profB =
                    getProfile(b);

                // Missing profile should not break the
                // strict weak ordering of std::sort.
                if (!profA || !profB) {

                    return sortAction.descending
                        ? a > b
                        : a < b;
                }

                // -----------------------------------------
                // Type
                // -----------------------------------------
                if (sortAction.method ==
                    GroupSortMethod::ByType)
                {
                    const QString valueA =
                        profA->outbound
                        ? profA->outbound->DisplayType()
                        : QString();

                    const QString valueB =
                        profB->outbound
                        ? profB->outbound->DisplayType()
                        : QString();


                    return sortAction.descending
                        ? valueA > valueB
                        : valueA < valueB;
                }

                // -----------------------------------------
                // Name
                // -----------------------------------------
                if (sortAction.method ==
                    GroupSortMethod::ByName)
                {
                    const QString valueA =
                        profA->outbound
                        ? profA->outbound->name
                        : QString();

                    const QString valueB =
                        profB->outbound
                        ? profB->outbound->name
                        : QString();


                    return sortAction.descending
                        ? valueA > valueB
                        : valueA < valueB;
                }

                // -----------------------------------------
                // Address
                // -----------------------------------------
                if (sortAction.method ==
                    GroupSortMethod::ByAddress)
                {
                    const QString valueA =
                        profA->outbound
                        ? profA->outbound->DisplayAddress()
                        : QString();

                    const QString valueB =
                        profB->outbound
                        ? profB->outbound->DisplayAddress()
                        : QString();


                    return sortAction.descending
                        ? valueA > valueB
                        : valueA < valueB;
                }

                // -----------------------------------------
                // Test result
                // -----------------------------------------
                if (sortAction.method ==
                    GroupSortMethod::ByTestResult)
                {
                    if (test_sort_by ==
                        testBy::latency)
                    {
                        const auto valueA =
                            latencyForSort(profA);

                        const auto valueB =
                            latencyForSort(profB);


                        return sortAction.descending
                            ? valueA > valueB
                            : valueA < valueB;
                    }

                    if (test_sort_by ==
                        testBy::dlSpeed)
                    {
                        const auto valueA =
                            bitrateToBps(
                                profA->dl_speed
                            );

                        const auto valueB =
                            bitrateToBps(
                                profB->dl_speed
                            );


                        return sortAction.descending
                            ? valueA > valueB
                            : valueA < valueB;
                    }

                    if (test_sort_by ==
                        testBy::ulSpeed)
                    {
                        const auto valueA =
                            bitrateToBps(
                                profA->ul_speed
                            );

                        const auto valueB =
                            bitrateToBps(
                                profB->ul_speed
                            );


                        return sortAction.descending
                            ? valueA > valueB
                            : valueA < valueB;
                    }

                    if (test_sort_by ==
                        testBy::ipOut)
                    {
                        return sortAction.descending
                            ? profA->ip_out > profB->ip_out
                            : profA->ip_out < profB->ip_out;
                    }
                }

                // -----------------------------------------
                // Traffic
                // -----------------------------------------
                if (sortAction.method ==
                    GroupSortMethod::ByTraffic)
                {
                    if (traffic_sort_by ==
                        trafficBy::total)
                    {
                        const auto valueA =
                            profA->traffic_downlink
                            + profA->traffic_uplink;

                        const auto valueB =
                            profB->traffic_downlink
                            + profB->traffic_uplink;


                        return sortAction.descending
                            ? valueA > valueB
                            : valueA < valueB;
                    }

                    if (traffic_sort_by ==
                        trafficBy::dl)
                    {
                        return sortAction.descending
                            ? profA->traffic_downlink
                            > profB->traffic_downlink
                            : profA->traffic_downlink
                            < profB->traffic_downlink;
                    }

                    if (traffic_sort_by ==
                        trafficBy::ul)
                    {
                        return sortAction.descending
                            ? profA->traffic_uplink
                            > profB->traffic_uplink
                            : profA->traffic_uplink
                            < profB->traffic_uplink;
                    }
                }

                // Stable deterministic fallback.
                return sortAction.descending
                    ? a > b
                    : a < b;
            }
        );

        return true;
    }

    bool Group::AddProfile(int ID)
    {
        QMutexLocker locker(&mutex);

        if (profiles.contains(ID)) {
            return false;
        }

        profiles.append(ID);

        return true;
    }

    bool Group::AddProfileBatch(
        const QList<int>& IDs)
    {
        QMutexLocker locker(&mutex);

        QSet<int> currentProfiles;

        currentProfiles.reserve(
            profiles.size() + IDs.size()
        );

        for (const int profileID : profiles) {
            currentProfiles.insert(profileID);
        }

        for (const int profileID : IDs) {

            if (currentProfiles.contains(profileID)) {
                continue;
            }

            profiles.append(profileID);

            // Важно обновлять set сразу,
            // чтобы дубликаты внутри IDs тоже
            // не добавились.
            currentProfiles.insert(profileID);
        }

        return true;
    }

    bool Group::RemoveProfile(int ID)
    {
        QMutexLocker locker(&mutex);

        if (!profiles.contains(ID)) {
            return false;
        }

        profiles.removeAll(ID);

        return true;
    }

    bool Group::RemoveProfileBatch(
        const QList<int>& IDs)
    {
        const QSet<int> toDelete(
            IDs.begin(),
            IDs.end()
        );

        QMutexLocker locker(&mutex);

        QList<int> newProfiles;

        newProfiles.reserve(
            profiles.size()
        );

        for (const int profileID : profiles) {

            if (!toDelete.contains(profileID)) {

                newProfiles.append(
                    profileID
                );
            }
        }

        profiles = std::move(newProfiles);

        return true;
    }

    bool Group::SwapProfiles(int idx1, int idx2)
    {
        QMutexLocker locker(&mutex);
        if (profiles.size() <= idx1 || profiles.size() <= idx2) return false;
        profiles.swapItemsAt(idx1, idx2);
        return true;
    }

    bool Group::EmplaceProfile(int idx, int newIdx)
    {
        QMutexLocker locker(&mutex);
        if (profiles.size() <= idx || profiles.size() <= newIdx) return false;
        profiles.insert(newIdx+1, profiles[idx]);
        if (idx < newIdx) profiles.remove(idx);
        else profiles.remove(idx+1);
        return true;
    }

    bool Group::HasProfile(int ID) const
    {
        QMutexLocker locker(&mutex);

        return profiles.contains(ID);
    }
}
