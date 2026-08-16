#include "include/database/entities/Group.h"

#include "include/database/ProfilesRepo.h"
#include "include/global/Configs.hpp"

#include <algorithm>
#include <utility>

#include <QCollator>
#include <QHash>
#include <QMutexLocker>
#include <QSet>


namespace Configs
{

    // =========================================================
    // Basic state
    // =========================================================

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

        default_profile_order =
            snapshot.default_profile_order;


        // Migration/fallback for groups created by an
        // older Gryph version.
        if (default_profile_order.isEmpty())
        {
            default_profile_order =
                profiles;
        }

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
        if (newId < 0)
        {
            return false;
        }

        QMutexLocker locker(
            &mutex
        );

        // ID is write-once.
        if (id >= 0)
        {
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
        if (column < 0)
        {
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


    void Group::clearCalculatedColumnWidth()
    {
        QMutexLocker locker(
            &mutex
        );

        calculated_column_width.clear();
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
    // Sorting/display settings
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


    // =========================================================
    // Snapshot
    // =========================================================

    GroupSnapshot Group::Snapshot() const
    {
        QMutexLocker locker(
            &mutex
        );

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

        snapshot.default_profile_order =
            default_profile_order;

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


    // =========================================================
    // Subscription
    // =========================================================

    void Group::SetSubscriptionSource(
        const QString& newName,
        const QString& newUrl)
    {
        QMutexLocker locker(
            &mutex
        );

        name =
            newName;

        url =
            newUrl;
    }


    void Group::UpdateSubscriptionState(
        qint64 lastUpdate,
        const QString& newInfo)
    {
        QMutexLocker locker(
            &mutex
        );

        sub_last_update =
            lastUpdate;

        info =
            newInfo;
    }


    // =========================================================
    // Profiles
    // =========================================================

    void Group::ReplaceProfiles(
        const QList<int>& newProfiles)
    {
        QMutexLocker locker(
            &mutex
        );

        profiles =
            newProfiles;
    }


    QList<int> 
        Group::Profiles() const
    {
        QMutexLocker locker(
            &mutex
        );

        return profiles;
    }

    QList<int>
        Group::DefaultProfileOrder() const
    {
        QMutexLocker locker(
            &mutex
        );

        return default_profile_order;
    }


    void Group::SetDefaultProfileOrder(
        const QList<int>& order)
    {
        QMutexLocker locker(
            &mutex
        );

        default_profile_order =
            order;
    }


    void Group::ReplaceProfilesFromSubscription(
        const QList<int>& order)
    {
        QMutexLocker locker(
            &mutex
        );

        // Current visible/canonical list.
        profiles =
            order;

        // Remember exactly the order supplied
        // by the subscription.
        default_profile_order =
            order;
    }


    bool Group::RestoreDefaultProfileOrder()
    {
        QMutexLocker sortLocker(
            &sortMutex_
        );

        QMutexLocker locker(
            &mutex
        );


        if (default_profile_order.isEmpty())
        {
            return false;
        }


        // IDs that currently still exist in the group.
        const QSet<int> currentIds(
            profiles.begin(),
            profiles.end()
        );


        QList<int> restored;

        restored.reserve(
            profiles.size()
        );


        QSet<int> inserted;

        inserted.reserve(
            profiles.size()
        );


        // Restore subscription order, but do not restore
        // profiles that have since been deleted.
        for (const int id :
        default_profile_order)
        {
            if (!currentIds.contains(id))
            {
                continue;
            }


            if (inserted.contains(id))
            {
                continue;
            }


            restored.append(id);
            inserted.insert(id);
        }


        // Profiles added after the subscription update are
        // appended to the end instead of disappearing.
        for (const int id :
        profiles)
        {
            if (inserted.contains(id))
            {
                continue;
            }


            restored.append(id);
            inserted.insert(id);
        }


        if (restored == profiles)
        {
            return true;
        }


        profiles =
            std::move(restored);


        return true;
    }

    // =========================================================
    // Speed-test value parser
    // =========================================================

    double bitrateToBps(
        const QString& input)
    {
        const QString str =
            input.trimmed();

        if (str.isEmpty() ||
            str.compare(
                QStringLiteral("N/A"),
                Qt::CaseInsensitive
            ) == 0)
        {
            return -1.0;
        }

        bool ok =
            false;


        if (str.endsWith(
            QStringLiteral("Gbps"),
            Qt::CaseInsensitive))
        {
            const double value =
                str.left(
                    str.size() - 4
                )
                .trimmed()
                .toDouble(&ok);

            return ok
                ? value * 1e9
                : -1.0;
        }


        if (str.endsWith(
            QStringLiteral("Mbps"),
            Qt::CaseInsensitive))
        {
            const double value =
                str.left(
                    str.size() - 4
                )
                .trimmed()
                .toDouble(&ok);

            return ok
                ? value * 1e6
                : -1.0;
        }


        if (str.endsWith(
            QStringLiteral("Kbps"),
            Qt::CaseInsensitive))
        {
            const double value =
                str.left(
                    str.size() - 4
                )
                .trimmed()
                .toDouble(&ok);

            return ok
                ? value * 1e3
                : -1.0;
        }


        if (str.endsWith(
            QStringLiteral("bps"),
            Qt::CaseInsensitive))
        {
            const double value =
                str.left(
                    str.size() - 3
                )
                .trimmed()
                .toDouble(&ok);

            return ok
                ? value
                : -1.0;
        }


        return -1.0;
    }


    // =========================================================
    // Sort profiles
    // =========================================================

    bool Group::SortProfiles(
        GroupSortAction sortAction)
    {
        QMutexLocker sortLocker(
            &sortMutex_
        );
        QList<int>
            idsSnapshot;
        testBy testSortBySnapshot =
            testBy::latency;
        trafficBy trafficSortBySnapshot =
            trafficBy::total;


        // -----------------------------------------------------
        // Phase 1
        //
        // Take a coherent Group snapshot and immediately
        // release the main Group mutex.
        // -----------------------------------------------------

        {
            QMutexLocker locker(
                &mutex
            );

            idsSnapshot =
                profiles;

            testSortBySnapshot =
                test_sort_by;

            trafficSortBySnapshot =
                traffic_sort_by;
        }


        // Raw means "preserve current order".
        if (sortAction.method ==
            GroupSortMethod::Raw)
        {
            return true;
        }


        // -----------------------------------------------------
        // ID sort requires no Profile objects.
        // -----------------------------------------------------

        if (sortAction.method ==
            GroupSortMethod::ById)
        {
            QMutexLocker locker(
                &mutex
            );

            // Do not overwrite concurrent modifications.
            if (profiles != idsSnapshot)
            {
                return false;
            }

            std::ranges::sort(
                profiles,

                [
                    descending =
                        sortAction.descending
                ](
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


        // -----------------------------------------------------
        // Sorting helpers
        // -----------------------------------------------------

        QCollator textCollator;

        textCollator.setCaseSensitivity(
            Qt::CaseInsensitive
        );

        // Natural text ordering:
        //
        // node2 < node10
        // instead of
        // node10 < node2
        textCollator.setNumericMode(
            true
        );


        const auto nameSortKey =
            [](QString value)
            {
                value =
                    value.trimmed();

                qsizetype pos =
                    0;


                // Ignore leading flags, emoji, punctuation
                // and spaces when sorting names.
                //
                // 🇩🇪 Nuremberg -> Nuremberg
                // 🇺🇸 Washington -> Washington
                while (pos < value.size() &&
                    !value[pos]
                    .isLetterOrNumber())
                {
                    ++pos;
                }


                if (pos > 0)
                {
                    value =
                        value.mid(pos);
                }

                return value;
            };


        const auto compareIds =
            [&sortAction](
                const int idA,
                const int idB)
            {
                return sortAction.descending
                    ? idA > idB
                    : idA < idB;
            };


        const auto compareText =
            [
                &textCollator,
                &sortAction,
                &compareIds
            ](
                const QString& valueA,
                const QString& valueB,
                const int idA,
                const int idB)
            {
                const bool emptyA =
                    valueA.isEmpty();

                const bool emptyB =
                    valueB.isEmpty();


                // Empty values always go to the bottom.
                if (emptyA != emptyB)
                {
                    return !emptyA;
                }


                const int cmp =
                    textCollator.compare(
                        valueA,
                        valueB
                    );


                if (cmp != 0)
                {
                    return sortAction.descending
                        ? cmp > 0
                        : cmp < 0;
                }


                return compareIds(
                    idA,
                    idB
                );
            };


        const auto compareNumber =
            [
                &sortAction,
                &compareIds
            ](
                const auto valueA,
                const auto valueB,
                const int idA,
                const int idB)
            {
                if (valueA != valueB)
                {
                    return sortAction.descending
                        ? valueA > valueB
                        : valueA < valueB;
                }

                return compareIds(
                    idA,
                    idB
                );
            };


        const auto compareOptionalNumber =
            [
                &compareNumber
            ](
                const auto valueA,
                const auto valueB,
                const bool validA,
                const bool validB,
                const int idA,
                const int idB)
            {
                // Valid values always stay above missing/N/A
                // values, regardless of direction.
                if (validA != validB)
                {
                    return validA;
                }


                // Both missing/invalid.
                if (!validA)
                {
                    return idA < idB;
                }


                return compareNumber(
                    valueA,
                    valueB,
                    idA,
                    idB
                );
            };


        // -----------------------------------------------------
        // Phase 2
        //
        // Load all Profile objects without holding Group::mutex.
        // -----------------------------------------------------

        const auto loadedProfiles =
            dataManager
            ->profilesRepo
            ->GetProfileBatch(
                idsSnapshot
            );


        QSet<int>
            loadedProfileIds;


        // Frozen configuration/display data.
        //
        // Group::SortProfiles must not read
        // mutable Profile::outbound fields from
        // inside the comparator.
        QHash<
            int,
            ProfileConfigSnapshot
        > configById;


        QHash<
            int,
            ProfileTestSnapshot
        > testById;


        QHash<
            int,
            ProfileTrafficSnapshot
        > trafficById;


        loadedProfileIds.reserve(
            loadedProfiles.size()
        );

        if (sortAction.method ==
            GroupSortMethod::ByName ||
            sortAction.method ==
            GroupSortMethod::ByType ||
            sortAction.method ==
            GroupSortMethod::ByAddress)
        {
            configById.reserve(
                loadedProfiles.size()
            );
        }

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


        // -----------------------------------------------------
        // Freeze every piece of information required by the
        // comparator.
        //
        // The comparator below therefore does not need to call
        // ProfilesRepo.
        // -----------------------------------------------------

        for (const auto& profile :
            loadedProfiles)
        {
            if (!profile)
            {
                continue;
            }


            // =====================================================
            // Freeze profile configuration
            // =====================================================

            const auto config =
                profile->ConfigSnapshot();


            const int profileId =
                config.id;


            if (profileId < 0)
            {
                continue;
            }


            loadedProfileIds.insert(
                profileId
            );


            // -----------------------------------------------------
            // Name / Type / Address
            // -----------------------------------------------------

            if (sortAction.method ==
                GroupSortMethod::ByName ||
                sortAction.method ==
                GroupSortMethod::ByType ||
                sortAction.method ==
                GroupSortMethod::ByAddress)
            {
                configById.insert(
                    profileId,
                    config
                );
            }


            // -----------------------------------------------------
            // Test Result
            // -----------------------------------------------------

            if (sortAction.method ==
                GroupSortMethod::ByTestResult)
            {
                testById.insert(
                    profileId,
                    profile->TestSnapshot()
                );
            }


            // -----------------------------------------------------
            // Traffic
            // -----------------------------------------------------

            if (sortAction.method ==
                GroupSortMethod::ByTraffic)
            {
                trafficById.insert(
                    profileId,
                    profile->TrafficSnapshot()
                );
            }
        }


        // -----------------------------------------------------
        // Phase 3
        //
        // Apply sorted result while holding Group::mutex.
        // -----------------------------------------------------

        QMutexLocker locker(
            &mutex
        );


        // If profiles were added/deleted/reordered while data
        // was being prepared, do not overwrite those changes.
        if (profiles != idsSnapshot)
        {
            return false;
        }


        std::ranges::sort(
            profiles,

            [&](const int a,
                const int b)
            {
                const bool loadedA =
                    loadedProfileIds.contains(a);

                const bool loadedB =
                    loadedProfileIds.contains(b);


                // Existing Profile objects always stay above
                // missing/corrupted entries.
                if (loadedA != loadedB)
                {
                    return loadedA;
                }


                if (!loadedA)
                {
                    return compareIds(
                        a,
                        b
                    );
                }


                // =========================================================
// Name
// =========================================================

                if (sortAction.method ==
                    GroupSortMethod::ByName)
                {
                    const auto configA =
                        configById.value(a);

                    const auto configB =
                        configById.value(b);


                    return compareText(
                        nameSortKey(
                            configA.name
                        ),

                        nameSortKey(
                            configB.name
                        ),

                        a,
                        b
                    );
                }


                // =========================================================
                // Type
                // =========================================================

                if (sortAction.method ==
                    GroupSortMethod::ByType)
                {
                    const auto configA =
                        configById.value(a);

                    const auto configB =
                        configById.value(b);


                    return compareText(
                        configA.displayType,
                        configB.displayType,
                        a,
                        b
                    );
                }


                // =========================================================
                // Address
                // =========================================================

                if (sortAction.method ==
                    GroupSortMethod::ByAddress)
                {
                    const auto configA =
                        configById.value(a);

                    const auto configB =
                        configById.value(b);


                    return compareText(
                        configA.displayAddress,
                        configB.displayAddress,
                        a,
                        b
                    );
                }

                // =========================================
                // Test Result
                // =========================================

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


                    // -------------------------------------
                    // Latency
                    // -------------------------------------

                    if (testSortBySnapshot ==
                        testBy::latency)
                    {
                        const int valueA =
                            testA.latency;

                        const int valueB =
                            testB.latency;


                        const bool validA =
                            valueA > 0;

                        const bool validB =
                            valueB > 0;


                        return compareOptionalNumber(
                            valueA,
                            valueB,
                            validA,
                            validB,
                            a,
                            b
                        );
                    }


                    // -------------------------------------
                    // Download speed
                    // -------------------------------------

                    if (testSortBySnapshot ==
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
                        const bool validA =
                            valueA >= 0.0;
                        const bool validB =
                            valueB >= 0.0;

                        return compareOptionalNumber(
                            valueA,
                            valueB,
                            validA,
                            validB,
                            a,
                            b
                        );
                    }


                    // -------------------------------------
                    // Upload speed
                    // -------------------------------------

                    if (testSortBySnapshot ==
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


                        const bool validA =
                            valueA >= 0.0;

                        const bool validB =
                            valueB >= 0.0;


                        return compareOptionalNumber(
                            valueA,
                            valueB,
                            validA,
                            validB,
                            a,
                            b
                        );
                    }


                    // -------------------------------------
                    // Outbound IP
                    // -------------------------------------

                    if (testSortBySnapshot ==
                        testBy::ipOut)
                    {
                        return compareText(
                            testA.ipOut,
                            testB.ipOut,
                            a,
                            b
                        );
                    }
                }


                // =========================================
                // Traffic
                // =========================================

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


                    if (trafficSortBySnapshot ==
                        trafficBy::total)
                    {
                        return compareNumber(
                            trafficA.total(),
                            trafficB.total(),
                            a,
                            b
                        );
                    }


                    if (trafficSortBySnapshot ==
                        trafficBy::dl)
                    {
                        return compareNumber(
                            trafficA.downlink,
                            trafficB.downlink,
                            a,
                            b
                        );
                    }


                    if (trafficSortBySnapshot ==
                        trafficBy::ul)
                    {
                        return compareNumber(
                            trafficA.uplink,
                            trafficB.uplink,
                            a,
                            b
                        );
                    }
                }


                // =========================================
                // Deterministic fallback
                // =========================================

                return compareIds(
                    a,
                    b
                );
            }
        );


        return true;
    }


    // =========================================================
    // Profile list modification
    // =========================================================

    bool Group::AddProfile(
        int ID)
    {
        if (ID < 0)
        {
            return false;
        }

        QMutexLocker locker(
            &mutex
        );

        if (profiles.contains(ID))
        {
            return false;
        }

        profiles.append(
            ID
        );

        return true;
    }


    bool Group::AddProfileBatch(
        const QList<int>& IDs)
    {
        QMutexLocker locker(
            &mutex
        );


        QSet<int> currentProfiles;

        currentProfiles.reserve(
            profiles.size() +
            IDs.size()
        );


        for (const int profileID :
        profiles)
        {
            currentProfiles.insert(
                profileID
            );
        }


        bool changed =
            false;


        for (const int profileID :
        IDs)
        {
            if (profileID < 0)
            {
                continue;
            }


            if (currentProfiles.contains(
                profileID))
            {
                continue;
            }


            profiles.append(
                profileID
            );


            currentProfiles.insert(
                profileID
            );


            changed =
                true;
        }


        return changed;
    }


    bool Group::RemoveProfile(
        int ID)
    {
        QMutexLocker locker(
            &mutex
        );

        if (!profiles.contains(ID))
        {
            return false;
        }

        profiles.removeAll(
            ID
        );

        return true;
    }


    bool Group::RemoveProfileBatch(
        const QList<int>& IDs)
    {
        if (IDs.isEmpty())
        {
            return false;
        }


        const QSet<int> toDelete(
            IDs.begin(),
            IDs.end()
        );


        QMutexLocker locker(
            &mutex
        );


        QList<int> newProfiles;

        newProfiles.reserve(
            profiles.size()
        );


        bool changed =
            false;


        for (const int profileID :
        profiles)
        {
            if (toDelete.contains(
                profileID))
            {
                changed =
                    true;

                continue;
            }


            newProfiles.append(
                profileID
            );
        }


        if (!changed)
        {
            return false;
        }


        profiles =
            std::move(
                newProfiles
            );


        return true;
    }


    bool Group::SwapProfiles(
        int idx1,
        int idx2)
    {
        QMutexLocker locker(
            &mutex
        );


        if (idx1 < 0 ||
            idx2 < 0 ||
            idx1 >= profiles.size() ||
            idx2 >= profiles.size())
        {
            return false;
        }


        if (idx1 == idx2)
        {
            return true;
        }


        profiles.swapItemsAt(
            idx1,
            idx2
        );


        return true;
    }


    bool Group::EmplaceProfile(
        int idx,
        int newIdx)
    {
        QMutexLocker locker(
            &mutex
        );


        if (idx < 0 ||
            newIdx < 0 ||
            idx >= profiles.size() ||
            newIdx >= profiles.size())
        {
            return false;
        }


        if (idx == newIdx)
        {
            return true;
        }


        const int profileId =
            profiles.at(idx);


        profiles.removeAt(
            idx
        );


        // After removing an item before the destination,
        // the destination index shifts left by one.
        int targetIndex =
            newIdx;


        if (idx < newIdx)
        {
            --targetIndex;
        }


        // Insert after the requested target row,
        // preserving the semantics of the old implementation.
        profiles.insert(
            targetIndex + 1,
            profileId
        );


        return true;
    }


    bool Group::HasProfile(
        int ID) const
    {
        QMutexLocker locker(
            &mutex
        );

        return profiles.contains(
            ID
        );
    }

} // namespace Configs