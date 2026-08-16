#pragma once

#include <QList>
#include <QMutex>
#include <QString>

#include <utility>

#include "include/ui/group/GroupSort.hpp"


namespace Configs
{
    enum class testBy : int
    {
        latency = 0,
        dlSpeed,
        ulSpeed,
        ipOut
    };


    enum class testShowItems : int
    {
        all = 0,
        none,
        ipOnly,
        speedOnly
    };


    enum class trafficBy : int
    {
        total = 0,
        dl,
        ul
    };


    struct GroupSnapshot
    {
        int id = -1;

        bool archive = false;
        bool skip_auto_update = false;
        bool auto_clear_unavailable = false;

        QString name;
        QString url;
        QString info;

        qint64 sub_last_update = 0;

        int front_proxy_id = -1;
        int landing_proxy_id = -1;

        QList<int> column_width;
        QList<int> profiles;

        int scroll_last_profile = -1;

        testBy test_sort_by =
            testBy::latency;

        trafficBy traffic_sort_by =
            trafficBy::total;

        testShowItems test_items_to_show =
            testShowItems::all;
    };


    class GroupsRepo;


    class Group
    {
    public:

        Group() = default;


        // =================================================
        // Immutable read snapshot
        // =================================================

        [[nodiscard]]
        GroupSnapshot Snapshot() const;


        [[nodiscard]]
        int Id() const;


        // =================================================
        // General persistent settings
        // =================================================

        void SetArchive(
            bool value
        );


        void SetAutoClearUnavailable(
            bool value
        );


        void SetSkipAutoUpdate(
            bool value
        );


        // Atomic update of settings edited by
        // DialogEditGroup.
        void UpdateEditableSettings(
            const QString& newName,
            const QString& newUrl,
            bool autoClearUnavailable,
            bool skipAutoUpdate,
            int frontProxyId,
            int landingProxyId
        );


        void SetProxyIds(
            int frontProxyId,
            int landingProxyId
        );


        // =================================================
        // Subscription
        // =================================================

        void SetSubscriptionSource(
            const QString& newName,
            const QString& newUrl
        );


        void UpdateSubscriptionState(
            qint64 lastUpdate,
            const QString& newInfo
        );


        // =================================================
        // Profiles
        // =================================================

        [[nodiscard]]
        QList<int> Profiles() const;


        void ReplaceProfiles(
            const QList<int>& newProfiles
        );


        bool SortProfiles(
            GroupSortAction method
        );


        bool RemoveProfile(
            int ID
        );


        bool RemoveProfileBatch(
            const QList<int>& IDs
        );


        bool AddProfile(
            int ID
        );


        bool AddProfileBatch(
            const QList<int>& IDs
        );


        bool SwapProfiles(
            int idx1,
            int idx2
        );


        bool EmplaceProfile(
            int idx,
            int newIdx
        );


        [[nodiscard]]
        bool HasProfile(
            int ID
        ) const;


        // =================================================
        // Column widths
        // =================================================

        void SetColumnWidths(
            const QList<int>& widths
        );


        void ClearColumnWidths();


        void clearCalculatedColumnWidth();


        void SetCalculatedColumnWidths(
            const QList<int>& widths
        );


        [[nodiscard]]
        QList<int>
            CalculatedColumnWidths() const;


        void ResetCalculatedColumnWidth(
            int column
        );


        // =================================================
        // List state
        // =================================================

        void SetScrollLastProfile(
            int profileId
        );


        void SetSelectedProfilesIdIdxPairs(
            const QList<
            std::pair<int, int>
            >& value
        );


        [[nodiscard]]
        QList<std::pair<int, int>>
            SelectedProfilesIdIdxPairs() const;


        // =================================================
        // Sorting / display settings
        // =================================================

        void SetTestSortBy(
            testBy value
        );


        void SetTrafficSortBy(
            trafficBy value
        );


        void SetTestItemsToShow(
            testShowItems value
        );


    private:

        // GroupsRepo is allowed to initialize an object
        // before publishing it.
        friend class GroupsRepo;


        // Used only while restoring a Group from DB.
        void LoadSnapshot(
            const GroupSnapshot& snapshot
        );


        // ID is write-once.
        // Only GroupsRepo may assign it.
        bool TryAssignId(
            int newId
        );


    private:

        mutable QMutex mutex;


        // =================================================
        // Persistent state
        // =================================================

        int id = -1;

        bool archive = false;

        bool skip_auto_update = false;

        bool auto_clear_unavailable = false;


        QString name;

        QString url;

        QString info;


        qint64 sub_last_update = 0;


        int front_proxy_id = -1;

        int landing_proxy_id = -1;


        QList<int> column_width;

        QList<int> profiles;


        int scroll_last_profile = -1;


        testBy test_sort_by =
            testBy::latency;


        trafficBy traffic_sort_by =
            trafficBy::total;


        testShowItems test_items_to_show =
            testShowItems::all;


        // =================================================
        // Runtime-only state
        // =================================================

        QList<int>
            calculated_column_width;


        QList<std::pair<int, int>>
            selectedProfilesIdIdxPairs;
    };
}