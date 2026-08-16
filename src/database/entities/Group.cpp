#include "include/database/entities/Group.h"

#include "include/database/ProfilesRepo.h"
#include "include/global/Configs.hpp"

#include <algorithm>

#include <QHash>
#include <QSet>
#include <QMutexLocker>

namespace Configs
{
    int Group::Id() const
    {
        QMutexLocker locker(
            &mutex
        );

        return id;
    }


    void Group::LoadSnapshot(
        const GroupSnapshot& snapshot)
    {
        QMutexLocker locker(
            &mutex
        );


        id =
            snapshot.id;


        archive =
            snapshot.archive;


        skip_auto_update =
            snapshot.skip_auto_update;


        auto_clear_unavailable =
            snapshot.auto_clear_unavailable;


        name =
            snapshot.name;


        url =
            snapshot.url;


        info =
            snapshot.info;


        sub_last_update =
            snapshot.sub_last_update;


        front_proxy_id =
            snapshot.front_proxy_id;


        landing_proxy_id =
            snapshot.landing_proxy_id;


        column_width =
            snapshot.column_width;


        profiles =
            snapshot.profiles;


        scroll_last_profile =
            snapshot.scroll_last_profile;


        test_sort_by =
            snapshot.test_sort_by;


        traffic_sort_by =
            snapshot.traffic_sort_by;


        test_items_to_show =
            snapshot.test_items_to_show;
    }


    bool Group::TryAssignId(
        int newId)
    {
        if (newId < 0) {
            return false;
        }


        QMutexLocker locker(
            &mutex
        );


        if (id >= 0) {
            return false;
        }


        id =
            newId;


        return true;
    }


    // =========================================================
    // General settings
    // =========================================================

    void Group::SetArchive(
        bool value)
    {
        QMutexLocker locker(
            &mutex
        );


        archive =
            value;
    }


    void Group::SetAutoClearUnavailable(
        bool value)
    {
        QMutexLocker locker(
            &mutex
        );


        auto_clear_unavailable =
            value;
    }


    void Group::SetSkipAutoUpdate(
        bool value)
    {
        QMutexLocker locker(
            &mutex
        );


        skip_auto_update =
            value;
    }


    void Group::UpdateEditableSettings(
        const QString& newName,
        const QString& newUrl,
        bool autoClearUnavailable,
        bool skipAutoUpdate,
        int frontProxyId,
        int landingProxyId)
    {
        QMutexLocker locker(
            &mutex
        );


        name =
            newName;


        url =
            newUrl;


        auto_clear_unavailable =
            autoClearUnavailable;


        skip_auto_update =
            skipAutoUpdate;


        front_proxy_id =
            frontProxyId;


        landing_proxy_id =
            landingProxyId;
    }


    void Group::SetProxyIds(
        int frontProxyId,
        int landingProxyId)
    {
        QMutexLocker locker(
            &mutex
        );


        front_proxy_id =
            frontProxyId;


        landing_proxy_id =
            landingProxyId;
    }


    // =========================================================
    // Column widths
    // =========================================================

    void Group::SetColumnWidths(
        const QList<int>& widths)
    {
        QMutexLocker locker(
            &mutex
        );


        column_width =
            widths;
    }


    void Group::ClearColumnWidths()
    {
        QMutexLocker locker(
            &mutex
        );


        column_width.clear();
    }


    void Group::SetCalculatedColumnWidths(
        const QList<int>& widths)
    {
        QMutexLocker locker(
            &mutex
        );


        calculated_column_width =
            widths;
    }


    QList<int>
        Group::CalculatedColumnWidths() const
    {
        QMutexLocker locker(
            &mutex
        );


        return calculated_column_width;
    }


    void Group::ResetCalculatedColumnWidth(
        int column)
    {
        if (column < 0) {
            return;
        }


        QMutexLocker locker(
            &mutex
        );


        if (column >=
            calculated_column_width.size())
        {
            return;
        }


        calculated_column_width[
            column
        ] = 0;
    }


    // =========================================================
    // UI/list state
    // =========================================================

    void Group::SetScrollLastProfile(
        int profileId)
    {
        QMutexLocker locker(
            &mutex
        );


        scroll_last_profile =
            profileId;
    }


    void Group::SetSelectedProfilesIdIdxPairs(
        const QList<
        std::pair<int, int>
        >& value)
    {
        QMutexLocker locker(
            &mutex
        );


        selectedProfilesIdIdxPairs =
            value;
    }


    QList<std::pair<int, int>>
        Group::SelectedProfilesIdIdxPairs() const
    {
        QMutexLocker locker(
            &mutex
        );


        return selectedProfilesIdIdxPairs;
    }


    // =========================================================
    // Sort/display
    // =========================================================

    void Group::SetTestSortBy(
        testBy value)
    {
        QMutexLocker locker(
            &mutex
        );


        test_sort_by =
            value;
    }


    void Group::SetTrafficSortBy(
        trafficBy value)
    {
        QMutexLocker locker(
            &mutex
        );


        traffic_sort_by =
            value;
    }


    void Group::SetTestItemsToShow(
        testShowItems value)
    {
        QMutexLocker locker(
            &mutex
        );


        test_items_to_show =
            value;
    }

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

    void Group::SetSubscriptionSource(
        const QString& newName,
        const QString& newUrl)
    {
        QMutexLocker locker(&mutex);

        name = newName;
        url = newUrl;
    }

    void Group::UpdateSubscriptionState(
        qint64 lastUpdate,
        const QString& newInfo)
    {
        QMutexLocker locker(&mutex);

        sub_last_update = lastUpdate;
        info = newInfo;
    }

    void Group::ReplaceProfiles(
        const QList<int>& newProfiles)
    {
        QMutexLocker locker(&mutex);

        profiles = newProfiles;
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
        // take Group profile-list snapshot
        // -------------------------------------------------

        {
            QMutexLocker locker(&mutex);

            idsSnapshot = profiles;
        }


        // Raw order means "leave as is".
        if (sortAction.method ==
            GroupSortMethod::Raw)
        {
            return true;
        }


        // ID sorting does not require ProfilesRepo.
        if (sortAction.method ==
            GroupSortMethod::ById)
        {
            QMutexLocker locker(&mutex);


            // Group may have changed while we were outside
            // the mutex.
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
        // obtain Profile objects WITHOUT Group::mutex
        // -------------------------------------------------

        const auto loadedProfiles =
            dataManager
            ->profilesRepo
            ->GetProfileBatch(
                idsSnapshot
            );


        QHash<
            int,
            std::shared_ptr<Profile>
        > profileById;


        QHash<
            int,
            ProfileTestSnapshot
        > testById;


        QHash<
            int,
            ProfileTrafficSnapshot
        > trafficById;


        profileById.reserve(
            loadedProfiles.size()
        );


        if (sortAction.method ==
            GroupSortMethod::ByTestResult)
        {
            testById.reserve(
                loadedProfiles.size()
            );
        }


        if (sortAction.method ==
            GroupSortMethod::ByTraffic)
        {
            trafficById.reserve(
                loadedProfiles.size()
            );
        }


        // -------------------------------------------------
        // Freeze all data required for the sort
        // -------------------------------------------------

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


            if (sortAction.method ==
                GroupSortMethod::ByTestResult)
            {
                testById.insert(
                    profile->id,
                    profile->TestSnapshot()
                );
            }


            if (sortAction.method ==
                GroupSortMethod::ByTraffic)
            {
                trafficById.insert(
                    profile->id,
                    profile->TrafficSnapshot()
                );
            }
        }


        // -------------------------------------------------
        // Phase 3:
        // lock Group after all external repository calls
        // -------------------------------------------------

        QMutexLocker locker(&mutex);


        // Do not overwrite concurrent changes to the group.
        if (profiles != idsSnapshot) {
            return false;
        }


        const auto getProfile =
            [&profileById](int id)
            -> std::shared_ptr<Profile>
            {
                const auto it =
                    profileById.constFind(id);


                if (it ==
                    profileById.constEnd())
                {
                    return nullptr;
                }


                return it.value();
            };


        const auto latencyForSort =
            [](int latency)
            {
                // Never tested.
                if (latency == 0) {
                    return 100000;
                }

                // Unavailable.
                if (latency < 0) {
                    return 99999;
                }

                return latency;
            };


        // -------------------------------------------------
        // Sort
        // -------------------------------------------------

        std::ranges::sort(
            profiles,

            [&](const int a,
                const int b)
            {
                const auto profA =
                    getProfile(a);

                const auto profB =
                    getProfile(b);


                // Missing profiles are sorted
                // deterministically by ID.
                if (!profA ||
                    !profB)
                {
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
                        ? profA
                        ->outbound
                        ->DisplayType()
                        : QString();


                    const QString valueB =
                        profB->outbound
                        ? profB
                        ->outbound
                        ->DisplayType()
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
                        ? profA
                        ->outbound
                        ->DisplayAddress()
                        : QString();


                    const QString valueB =
                        profB->outbound
                        ? profB
                        ->outbound
                        ->DisplayAddress()
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
                    const auto testA =
                        testById.value(
                            a,
                            ProfileTestSnapshot{}
                        );


                    const auto testB =
                        testById.value(
                            b,
                            ProfileTestSnapshot{}
                        );


                    // Latency
                    if (test_sort_by ==
                        testBy::latency)
                    {
                        const int valueA =
                            latencyForSort(
                                testA.latency
                            );


                        const int valueB =
                            latencyForSort(
                                testB.latency
                            );


                        return sortAction.descending
                            ? valueA > valueB
                            : valueA < valueB;
                    }


                    // Download speed
                    if (test_sort_by ==
                        testBy::dlSpeed)
                    {
                        const double valueA =
                            bitrateToBps(
                                testA.dlSpeed
                            );


                        const double valueB =
                            bitrateToBps(
                                testB.dlSpeed
                            );


                        return sortAction.descending
                            ? valueA > valueB
                            : valueA < valueB;
                    }


                    // Upload speed
                    if (test_sort_by ==
                        testBy::ulSpeed)
                    {
                        const double valueA =
                            bitrateToBps(
                                testA.ulSpeed
                            );


                        const double valueB =
                            bitrateToBps(
                                testB.ulSpeed
                            );


                        return sortAction.descending
                            ? valueA > valueB
                            : valueA < valueB;
                    }


                    // Outbound IP
                    if (test_sort_by ==
                        testBy::ipOut)
                    {
                        return sortAction.descending
                            ? testA.ipOut >
                            testB.ipOut
                            : testA.ipOut <
                            testB.ipOut;
                    }
                }


                // -----------------------------------------
                // Traffic
                // -----------------------------------------

                if (sortAction.method ==
                    GroupSortMethod::ByTraffic)
                {
                    const auto trafficA =
                        trafficById.value(
                            a,
                            ProfileTrafficSnapshot{}
                        );


                    const auto trafficB =
                        trafficById.value(
                            b,
                            ProfileTrafficSnapshot{}
                        );


                    if (traffic_sort_by ==
                        trafficBy::total)
                    {
                        return sortAction.descending
                            ? trafficA.total() >
                            trafficB.total()
                            : trafficA.total() <
                            trafficB.total();
                    }


                    if (traffic_sort_by ==
                        trafficBy::dl)
                    {
                        return sortAction.descending
                            ? trafficA.downlink >
                            trafficB.downlink
                            : trafficA.downlink <
                            trafficB.downlink;
                    }


                    if (traffic_sort_by ==
                        trafficBy::ul)
                    {
                        return sortAction.descending
                            ? trafficA.uplink >
                            trafficB.uplink
                            : trafficA.uplink <
                            trafficB.uplink;
                    }
                }


                // -----------------------------------------
                // Deterministic fallback
                // -----------------------------------------

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
