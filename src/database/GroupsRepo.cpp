#include "include/database/entities/Group.h"
#include "include/database/GroupsRepo.h"
#include "include/global/Utils.hpp"

#include <stdexcept>

#include <QSet>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMutexLocker>

#include "include/database/ProfilesRepo.h"
#include "include/global/Configs.hpp"
#include "include/ui/MainWindow.h"

namespace Configs 
{
    GroupsRepo::GroupsRepo(
        Database& database)
        :
        db(database)
    {
        if (!createTables())
        {
            throw std::runtime_error(
                "Failed to initialize GroupsRepo database schema"
            );
        }
    }

    bool GroupsRepo::createTables() const
    {
        // =========================================================
        // groups
        // =========================================================
        if (!db.exec(
            R"(
            CREATE TABLE IF NOT EXISTS groups
            (
                id INTEGER PRIMARY KEY,

                archive INTEGER NOT NULL DEFAULT 0,

                skip_auto_update INTEGER NOT NULL DEFAULT 0,

                name TEXT NOT NULL DEFAULT '',

                url TEXT,

                info TEXT,

                sub_last_update INTEGER NOT NULL DEFAULT 0,

                front_proxy_id INTEGER NOT NULL DEFAULT -1,

                landing_proxy_id INTEGER NOT NULL DEFAULT -1,

                column_width_json TEXT,

                profiles_json TEXT NOT NULL DEFAULT '[]',

                default_profile_order_json
                    TEXT NOT NULL DEFAULT '[]',

                scroll_last_profile INTEGER NOT NULL DEFAULT -1,

                auto_clear_unavailable
                    INTEGER NOT NULL DEFAULT 0,

                test_sort_by INTEGER NOT NULL DEFAULT 0,

                traffic_sort_by INTEGER NOT NULL DEFAULT 0,

                test_items_to_show INTEGER NOT NULL DEFAULT 0,

                created_at INTEGER NOT NULL
                    DEFAULT (strftime('%s', 'now')),

                updated_at INTEGER NOT NULL
                    DEFAULT (strftime('%s', 'now'))
            )
            )"))
        {
            MW_show_log(
                "GroupsRepo::createTables: "
                "failed to create groups table"
            );

            return false;
        }


        // =========================================================
        // groups_order
        // =========================================================
        if (!db.exec(
            R"(
        CREATE TABLE IF NOT EXISTS groups_order
        (
            group_id INTEGER NOT NULL PRIMARY KEY,

            display_order INTEGER NOT NULL
        )
        )"))
        {
            MW_show_log(
                "GroupsRepo::createTables: "
                "failed to create groups_order table"
            );

            return false;
        }

        // =========================================================
        // Migration:
        //
        // Older Gryph databases may already contain `groups`,
        // but without default_profile_order_json.
        //
        // CREATE TABLE IF NOT EXISTS does NOT add a missing column
        // to an existing table, therefore ALTER TABLE is still
        // required for old databases.
        // =========================================================
        if (!groupsColumnExists(
            "default_profile_order_json"))
        {
            if (!db.exec(
                "ALTER TABLE groups "
                "ADD COLUMN "
                "default_profile_order_json "
                "TEXT NOT NULL DEFAULT '[]'"))
            {
                MW_show_log(
                    "GroupsRepo::createTables: "
                    "failed to add "
                    "default_profile_order_json column"
                );

                return false;
            }
        }

        // =========================================================
        // Entire schema initialization succeeded.
        // =========================================================
        return true;
    }

    QJsonObject GroupsRepo::groupToJson(
        const GroupSnapshot& group) const
    {
        QJsonObject json;


        json["id"] =
            group.id;

        json["archive"] =
            group.archive;

        json["skip_auto_update"] =
            group.skip_auto_update;

        json["auto_clear_unavailable"] =
            group.auto_clear_unavailable;


        json["name"] =
            group.name;

        json["url"] =
            group.url;

        json["info"] =
            group.info;


        json["sub_last_update"] =
            static_cast<qint64>(
                group.sub_last_update
                );


        json["front_proxy_id"] =
            group.front_proxy_id;

        json["landing_proxy_id"] =
            group.landing_proxy_id;


        json["column_width"] =
            QListInt2QJsonArray(
                group.column_width
            );

        json["profiles"] =
            QListInt2QJsonArray(
                group.profiles
            );


        json["scroll_last_profile"] =
            group.scroll_last_profile;


        json["test_sort_by"] =
            static_cast<int>(
                group.test_sort_by
                );

        json["traffic_sort_by"] =
            static_cast<int>(
                group.traffic_sort_by
                );

        json["test_items_to_show"] =
            static_cast<int>(
                group.test_items_to_show
                );


        return json;
    }

    std::shared_ptr<Group>
        GroupsRepo::groupFromJson(
            const QJsonObject& json) const
    {
        auto group =
            std::make_shared<Group>();


        GroupSnapshot snapshot;


        snapshot.id =
            json["id"]
            .toInt(-1);


        snapshot.archive =
            json["archive"]
            .toBool();


        snapshot.skip_auto_update =
            json["skip_auto_update"]
            .toBool();


        snapshot.auto_clear_unavailable =
            json["auto_clear_unavailable"]
            .toBool();


        snapshot.name =
            json["name"]
            .toString();


        snapshot.url =
            json["url"]
            .toString();


        snapshot.info =
            json["info"]
            .toString();


        snapshot.sub_last_update =
            json["sub_last_update"]
            .toVariant()
            .toLongLong();


        snapshot.front_proxy_id =
            json["front_proxy_id"]
            .toInt(-1);


        snapshot.landing_proxy_id =
            json["landing_proxy_id"]
            .toInt(-1);


        snapshot.column_width =
            QJsonArray2QListInt(
                json["column_width"]
                .toArray()
            );


        snapshot.profiles =
            QJsonArray2QListInt(
                json["profiles"]
                .toArray()
            );


        snapshot.scroll_last_profile =
            json["scroll_last_profile"]
            .toInt(-1);


        snapshot.test_sort_by =
            static_cast<testBy>(
                json["test_sort_by"]
                .toInt(0)
                );


        snapshot.traffic_sort_by =
            static_cast<trafficBy>(
                json["traffic_sort_by"]
                .toInt(0)
                );


        snapshot.test_items_to_show =
            static_cast<testShowItems>(
                json["test_items_to_show"]
                .toInt(0)
                );


        group->LoadSnapshot(
            snapshot
        );


        return group;
    }

    bool GroupsRepo::groupsColumnExists(
        const char* columnName) const
    {
        if (!columnName ||
            *columnName == '\0')
        {
            return false;
        }


        auto pragma =
            db.query(
                "PRAGMA table_info(groups)"
            );


        if (!pragma)
        {
            MW_show_log(
                "GroupsRepo::groupsColumnExists: "
                "failed to query groups schema"
            );

            return false;
        }


        try
        {
            while (pragma->executeStep())
            {
                const std::string currentColumn =
                    pragma
                    ->getColumn(1)
                    .getText();


                if (currentColumn ==
                    std::string(columnName))
                {
                    return true;
                }
            }
        }
        catch (const std::exception& e)
        {
            MW_show_log(
                "GroupsRepo::groupsColumnExists: "
                "failed while reading groups schema: "
                +
                QString::fromUtf8(
                    e.what()
                )
            );

            return false;
        }


        return false;
    }

    bool GroupsRepo::saveToDatabase(
        const GroupSnapshot& group) const
    {
        // -------------------------------------------------
        // Serialize immutable snapshot
        // -------------------------------------------------

        const QJsonArray columnWidthArray =
            QListInt2QJsonArray(
                group.column_width
            );


        const QJsonArray profilesArray =
            QListInt2QJsonArray(
                group.profiles
            );


        const QJsonDocument columnWidthDoc(
            columnWidthArray
        );

        const QJsonDocument profilesDoc(
            profilesArray
        );


        const QString columnWidthJson =
            QString::fromUtf8(
                columnWidthDoc.toJson(
                    QJsonDocument::Compact
                )
            );


        const QString profilesJson =
            QString::fromUtf8(
                profilesDoc.toJson(
                    QJsonDocument::Compact
                )
            );


        // -------------------------------------------------
        // Check existence
        // -------------------------------------------------

        bool exists = false;
        {
            auto checkQuery =
                db.query(
                    "SELECT id "
                    "FROM groups "
                    "WHERE id = ?",
                    group.id
                );
            if (!checkQuery)
            {
                return false;
            }
            try
            {
                exists =
                    checkQuery->executeStep();
            }
            catch (std::exception& e)
            {
                NotifyError(
                    "SELECT id FROM groups WHERE id = ?",
                    e
                );

                return false;
            }
        }

        // -------------------------------------------------
        // UPDATE
        // -------------------------------------------------
        if (exists)
        {
            return db.exec(
                R"(
                UPDATE groups
                SET archive = ?,
                    skip_auto_update = ?,
                    auto_clear_unavailable = ?,
                    name = ?,
                    url = ?,
                    info = ?,
                    sub_last_update = ?,
                    front_proxy_id = ?,
                    landing_proxy_id = ?,
                    column_width_json = ?,
                    profiles_json = ?,
                    scroll_last_profile = ?,
                    test_sort_by = ?,
                    traffic_sort_by = ?,
                    test_items_to_show = ?,
                    updated_at = strftime('%s', 'now')
                WHERE id = ?
                )",

                group.archive
                ? 1
                : 0,

                group.skip_auto_update
                ? 1
                : 0,

                group.auto_clear_unavailable
                ? 1
                : 0,

                group.name.toStdString(),

                group.url.toStdString(),

                group.info.toStdString(),

                static_cast<long long>(
                    group.sub_last_update
                    ),

                group.front_proxy_id,

                group.landing_proxy_id,

                columnWidthJson.toStdString(),

                profilesJson.toStdString(),

                group.scroll_last_profile,

                static_cast<int>(
                    group.test_sort_by
                    ),

                static_cast<int>(
                    group.traffic_sort_by
                    ),

                static_cast<int>(
                    group.test_items_to_show
                    ),

                group.id
            );
        }

        // -------------------------------------------------
        // INSERT
        // -------------------------------------------------
        return db.exec(
            R"(
            INSERT INTO groups
            (
                id,
                archive,
                skip_auto_update,
                auto_clear_unavailable,
                name,
                url,
                info,
                sub_last_update,
                front_proxy_id,
                landing_proxy_id,
                column_width_json,
                profiles_json,
                scroll_last_profile,
                test_sort_by,
                traffic_sort_by,
                test_items_to_show
            )
            VALUES
            (
                ?, ?, ?, ?, ?, ?, ?, ?,
                ?, ?, ?, ?, ?, ?, ?, ?
            )
            )",

            group.id,

            group.archive
            ? 1
            : 0,

            group.skip_auto_update
            ? 1
            : 0,

            group.auto_clear_unavailable
            ? 1
            : 0,

            group.name.toStdString(),

            group.url.toStdString(),

            group.info.toStdString(),

            static_cast<long long>(
                group.sub_last_update
                ),

            group.front_proxy_id,

            group.landing_proxy_id,

            columnWidthJson.toStdString(),

            profilesJson.toStdString(),

            group.scroll_last_profile,

            static_cast<int>(
                group.test_sort_by
                ),

            static_cast<int>(
                group.traffic_sort_by
                ),

            static_cast<int>(
                group.test_items_to_show
                )
        );
    }
    
    std::shared_ptr<Group> GroupsRepo::loadFromDatabase(int id) const 
    {
        auto query = db.query(R"(
            SELECT id, archive, skip_auto_update, auto_clear_unavailable, name, url, info, sub_last_update,
                   front_proxy_id, landing_proxy_id,
                   column_width_json, profiles_json, scroll_last_profile, test_sort_by, traffic_sort_by, test_items_to_show
            FROM groups WHERE id = ?
        )", id);
        if (!query || !query->executeStep()) {
            return nullptr;
        }
        
        QJsonObject json;
        json["id"] = query->getColumn(0).getInt();
        json["archive"] = query->getColumn(1).getInt() != 0;
        json["skip_auto_update"] = query->getColumn(2).getInt() != 0;
        json["auto_clear_unavailable"] = query->getColumn(3).getInt() != 0;
        json["name"] = QString::fromStdString(query->getColumn(4).getText());
        json["url"] = QString::fromStdString(query->getColumn(5).getText());
        json["info"] = QString::fromStdString(query->getColumn(6).getText());
        json["sub_last_update"] = static_cast<qint64>(query->getColumn(7).getInt64());
        json["front_proxy_id"] = query->getColumn(8).getInt();
        json["landing_proxy_id"] = query->getColumn(9).getInt();

        // Parse JSON arrays
        QString columnWidthJsonStr = QString::fromStdString(query->getColumn(10).getText());
        if (!columnWidthJsonStr.isEmpty()) {
            QJsonDocument columnWidthDoc = QJsonDocument::fromJson(columnWidthJsonStr.toUtf8());
            if (!columnWidthDoc.isNull() && columnWidthDoc.isArray()) {
                json["column_width"] = columnWidthDoc.array();
            }
        }
        
        QString profilesJsonStr = QString::fromStdString(query->getColumn(11).getText());
        if (!profilesJsonStr.isEmpty()) {
            QJsonDocument profilesDoc = QJsonDocument::fromJson(profilesJsonStr.toUtf8());
            if (!profilesDoc.isNull() && profilesDoc.isArray()) {
                json["profiles"] = profilesDoc.array();
            }
        }

        json["scroll_last_profile"] = query->getColumn(12).getInt();
        json["test_sort_by"] = query->getColumn(13).getInt();
        json["traffic_sort_by"] = query->getColumn(14).getInt();
        json["test_items_to_show"] = query->getColumn(15).getInt();
        
        return groupFromJson(json);
    }

    std::shared_ptr<Group> GroupsRepo::NewGroup() {
        return std::make_shared<Group>();
    }

    bool GroupsRepo::AddGroup(
        std::shared_ptr<Group>& group)
    {
        // =========================================================
        // Validation
        // =========================================================

        if (!group)
        {
            MW_show_log(
                "GroupsRepo::AddGroup: "
                "null Group"
            );

            return false;
        }


        // =========================================================
        // AddGroup() is only for a new unpublished Group.
        // =========================================================

        {
            const GroupSnapshot current =
                group->Snapshot();


            if (current.id >= 0)
            {
                MW_show_log(
                    "GroupsRepo::AddGroup: "
                    "Group is already published"
                );

                return false;
            }
        }


        // =========================================================
        // Reserve unique ID.
        //
        // NewGroupID() uses SQLite UPDATE ... RETURNING and therefore
        // provides a unique ID even when several threads create
        // groups concurrently.
        //
        // A failed AddGroup() may leave a gap in the ID sequence.
        // That is intentional and harmless. IDs are identities,
        // not contiguous array indices.
        // =========================================================

        const int newId =
            NewGroupID();


        if (newId <= 0)
        {
            MW_show_log(
                "GroupsRepo::AddGroup: "
                "failed to allocate Group ID"
            );

            return false;
        }


        // =========================================================
        // Assign identity to the unpublished object.
        //
        // TryAssignId() is also the concurrency guard if somebody
        // accidentally calls AddGroup() for the same Group object
        // from two threads.
        // =========================================================

        if (!group->TryAssignId(
            newId))
        {
            MW_show_log(
                "GroupsRepo::AddGroup: "
                "failed to assign Group ID"
            );

            return false;
        }


        // =========================================================
        // Freeze immutable state for persistence.
        // =========================================================

        const GroupSnapshot snapshot =
            group->Snapshot();


        if (snapshot.id != newId)
        {
            MW_show_log(
                "GroupsRepo::AddGroup: "
                "Group identity changed unexpectedly"
            );


            const bool rolledBack =
                group->RollbackAssignedId(
                    newId
                );


            if (!rolledBack)
            {
                MW_show_log(
                    "GroupsRepo::AddGroup: "
                    "CRITICAL: failed to rollback Group ID"
                );
            }


            return false;
        }


        // =========================================================
        // Serialize snapshot into a DB-only row.
        // =========================================================

        const QJsonArray columnWidthArray =
            QListInt2QJsonArray(
                snapshot.column_width
            );


        const QJsonArray profilesArray =
            QListInt2QJsonArray(
                snapshot.profiles
            );


        const QString columnWidthJson =
            QString::fromUtf8(
                QJsonDocument(
                    columnWidthArray
                ).toJson(
                    QJsonDocument::Compact
                )
            );


        const QString profilesJson =
            QString::fromUtf8(
                QJsonDocument(
                    profilesArray
                ).toJson(
                    QJsonDocument::Compact
                )
            );


        GroupInsertRow row;


        row.id =
            newId;


        row.archive =
            snapshot.archive;


        row.skip_auto_update =
            snapshot.skip_auto_update;


        row.auto_clear_unavailable =
            snapshot.auto_clear_unavailable;


        row.name =
            snapshot
            .name
            .toStdString();


        row.url =
            snapshot
            .url
            .toStdString();


        row.info =
            snapshot
            .info
            .toStdString();


        row.sub_last_update =
            static_cast<long long>(
                snapshot.sub_last_update
                );


        row.front_proxy_id =
            snapshot.front_proxy_id;


        row.landing_proxy_id =
            snapshot.landing_proxy_id;


        row.column_width_json =
            columnWidthJson
            .toStdString();


        row.profiles_json =
            profilesJson
            .toStdString();


        row.scroll_last_profile =
            snapshot.scroll_last_profile;


        row.test_sort_by =
            static_cast<int>(
                snapshot.test_sort_by
                );


        row.traffic_sort_by =
            static_cast<int>(
                snapshot.traffic_sort_by
                );


        row.test_items_to_show =
            static_cast<int>(
                snapshot.test_items_to_show
                );


        // =========================================================
        // ATOMIC DATABASE PHASE
        //
        // This creates:
        //
        //      groups row
        //          +
        //      groups_order row
        //
        // in ONE SQLite transaction.
        //
        // Nothing has been published into memMap yet.
        // =========================================================

        const bool persisted =
            db.insertGroupAtomic(
                row
            );


        if (!persisted)
        {
            MW_show_log(
                "GroupsRepo::AddGroup: "
                "database transaction failed; "
                "rolling back Group identity"
            );


            // =====================================================
            // Return caller's object to exactly the state expected
            // from an unpublished Group.
            // =====================================================

            const bool rolledBack =
                group->RollbackAssignedId(
                    newId
                );


            if (!rolledBack)
            {
                MW_show_log(
                    "GroupsRepo::AddGroup: "
                    "CRITICAL: failed to rollback Group identity"
                );
            }

            return false;
        }


        // =========================================================
        // DATABASE COMMIT SUCCEEDED.
        //
        // Only NOW publish into repository memory.
        // =========================================================

        {
            std::lock_guard<std::mutex>
                locker(
                    mutex
                );


            memMap[newId] =
                group;
        }


        return true;
    }

    std::shared_ptr<Group> GroupsRepo::GetGroup(int id) const {
        QMutexLocker locker(&mutex);
        if (auto it = memMap.find(id); it != memMap.end()) {
            return it->second;
        }
        auto group = loadFromDatabase(id);
        if (!group) return nullptr;
        memMap[id] = group;
        return group;
    }

    std::shared_ptr<Group> GroupsRepo::CurrentGroup() const {
        // Read current_group from SettingsRepo
        if (!Configs::dataManager || !Configs::dataManager->settingsRepo) {
            return nullptr;
        }
        
        int currentGroupId = Configs::dataManager->settingsRepo->current_group;
        
        // Retrieve and return the group with that ID
        return GetGroup(currentGroupId);
    }

    bool GroupsRepo::DeleteGroup(
        int id)
    {
        if (id < 0)
        {
            return false;
        }


        std::lock_guard<std::mutex>
            locker(
                mutex
            );


        if (!db.exec(
            "DELETE FROM groups_order "
            "WHERE group_id = ?",
            id))
        {
            return false;
        }


        if (!db.exec(
            "DELETE FROM groups "
            "WHERE id = ?",
            id))
        {
            return false;
        }


        // Publish deletion only after DB accepted it.
        memMap.erase(
            id
        );


        return true;
    }

    QList<int> GroupsRepo::GetAllGroupIds() const {
        QList<int> ids;
        auto query = db.query("SELECT id FROM groups ORDER BY id");
        if (query) {
            while (query->executeStep()) {
                ids.append(query->getColumn(0).getInt());
            }
        }
        return ids;
    }

    int GroupsRepo::NewGroupID() const {
        // Atomically increment and get the new ID using RETURNING clause
        // Note: This method is called from within methods that already hold the mutex lock
        auto query = db.query("UPDATE entity_ids SET group_last_id = group_last_id + 1 RETURNING group_last_id");
        if (query && query->executeStep()) {
            return query->getColumn(0).getInt();
        }
        
        // Fallback if RETURNING is not supported (shouldn't happen with modern SQLite)
        return 0;
    }

    QList<int> GroupsRepo::GetGroupsTabOrder() const {
        QList<int> order;
        auto query = db.query("SELECT group_id FROM groups_order ORDER BY display_order");
        if (query) {
            while (query->executeStep()) {
                order.append(query->getColumn(0).getInt());
            }
        }
        return order;
    }

    bool GroupsRepo::SetGroupsTabOrder(
        const QList<int>& order)
    {
        if (!db.exec(
            "DELETE FROM groups_order"))
        {
            return false;
        }


        if (order.isEmpty())
        {
            return true;
        }


        std::vector<int>
            pairs;


        pairs.reserve(
            static_cast<size_t>(
                order.size() * 2
                )
        );


        for (int i = 0;
            i < order.size();
            ++i)
        {
            pairs.push_back(
                order[i]
            );

            pairs.push_back(
                i
            );
        }


        return db.execBatchInsertIntPairs(
            "groups_order",
            "group_id",
            "display_order",
            pairs
        );
    }

    bool GroupsRepo::Save(
        const std::shared_ptr<Group>& group)
    {
        if (!group)
        {
            return false;
        }


        // =========================================================
        // Freeze Group state before repository lock.
        //
        // Group::Snapshot() takes the Group's own mutex.
        // =========================================================

        const GroupSnapshot snapshot =
            group->Snapshot();


        if (snapshot.id < 0)
        {
            return false;
        }


        // =========================================================
        // Persist first.
        // =========================================================

        {
            std::lock_guard<std::mutex>
                locker(
                    mutex
                );


            if (!saveToDatabase(
                snapshot))
            {
                return false;
            }


            // =====================================================
            // Publish only after SQLite write succeeded.
            // =====================================================

            memMap[snapshot.id] =
                group;
        }


        return true;
    }

    bool GroupsRepo::CommitProfileDeletion(
        const QList<int>& profileIds,
        const QList<std::shared_ptr<Group>>& affectedGroups)
    {
        // =========================================================
        // Empty deletion is already complete.
        // =========================================================

        if (profileIds.isEmpty())
        {
            return true;
        }


        // =========================================================
        // Normalize IDs.
        //
        // Database should never receive negative IDs or duplicates.
        // =========================================================

        QSet<int>
            idsToDelete;


        idsToDelete.reserve(
            profileIds.size()
        );


        for (const int id :
        profileIds)
        {
            if (id >= 0)
            {
                idsToDelete.insert(
                    id
                );
            }
        }


        if (idsToDelete.isEmpty())
        {
            return true;
        }


        // =========================================================
        // Serialize ALL Group persistence while this cross-table
        // operation is being prepared and committed.
        //
        // GroupsRepo::Save() uses the same mutex, therefore it
        // cannot write a stale profiles_json in the middle of this
        // operation.
        // =========================================================

        std::lock_guard<std::mutex>
            locker(
                mutex
            );


        // =========================================================
        // Prepare DB-only Group updates.
        // =========================================================

        std::vector<GroupProfilesUpdateRow>
            groupUpdates;


        groupUpdates.reserve(
            static_cast<std::size_t>(
                affectedGroups.size()
                )
        );


        QList<std::shared_ptr<Group>>
            groupsToApply;


        groupsToApply.reserve(
            affectedGroups.size()
        );


        QSet<int>
            processedGroupIds;


        processedGroupIds.reserve(
            affectedGroups.size()
        );


        for (const auto& group :
            affectedGroups)
        {
            if (!group)
            {
                MW_show_log(
                    "GroupsRepo::CommitProfileDeletion: "
                    "null Group"
                );

                return false;
            }


            // =====================================================
            // Freeze current Group state.
            // =====================================================

            const GroupSnapshot snapshot =
                group->Snapshot();


            if (snapshot.id < 0)
            {
                MW_show_log(
                    "GroupsRepo::CommitProfileDeletion: "
                    "invalid Group identity"
                );

                return false;
            }


            // Same Group may accidentally be supplied more than once.
            if (processedGroupIds.contains(
                snapshot.id))
            {
                continue;
            }


            processedGroupIds.insert(
                snapshot.id
            );


            // =====================================================
            // Calculate the list which must be persisted.
            //
            // IMPORTANT:
            //
            // Do NOT mutate Group memory yet.
            //
            // SQLite must COMMIT first.
            // =====================================================

            QList<int>
                newProfiles;


            newProfiles.reserve(
                snapshot.profiles.size()
            );


            bool changed =
                false;


            for (const int profileId :
            snapshot.profiles)
            {
                if (idsToDelete.contains(
                    profileId))
                {
                    changed =
                        true;

                    continue;
                }


                newProfiles.append(
                    profileId
                );
            }


            // The group doesn't actually reference any of the
            // deleted Profiles.
            if (!changed)
            {
                continue;
            }


            // =====================================================
            // Serialize only profiles_json.
            // =====================================================

            const QJsonArray profilesArray =
                QListInt2QJsonArray(
                    newProfiles
                );


            const QByteArray profilesJson =
                QJsonDocument(
                    profilesArray
                )
                .toJson(
                    QJsonDocument::Compact
                );


            GroupProfilesUpdateRow row;


            row.id =
                snapshot.id;


            row.profiles_json =
                QString::fromUtf8(
                    profilesJson
                )
                .toStdString();


            groupUpdates.push_back(
                std::move(
                    row
                )
            );


            groupsToApply.append(
                group
            );
        }


        // =========================================================
        // Convert Profile IDs into the DB representation.
        // =========================================================

        std::vector<int>
            dbProfileIds;


        dbProfileIds.reserve(
            static_cast<std::size_t>(
                idsToDelete.size()
                )
        );


        for (const int id :
        idsToDelete)
        {
            dbProfileIds.push_back(
                id
            );
        }


        // =========================================================
        // ATOMIC DATABASE PHASE
        //
        // ONE transaction:
        //
        //     UPDATE group 1 profiles_json
        //     UPDATE group 2 profiles_json
        //     ...
        //     DELETE profiles
        //
        // Either everything commits or nothing does.
        // =========================================================

        const bool persisted =
            db.deleteProfilesWithGroupUpdatesAtomic(
                groupUpdates,
                dbProfileIds
            );


        if (!persisted)
        {
            MW_show_log(
                "GroupsRepo::CommitProfileDeletion: "
                "atomic database transaction failed"
            );


            return false;
        }


        // =========================================================
        // SQLite COMMIT succeeded.
        //
        // NOW update live Group objects.
        //
        // Use RemoveProfileBatch() rather than ReplaceProfiles().
        //
        // This is deliberate:
        //
        // If another thread changed an unrelated part of the current
        // in-memory ordering while SQLite was working, we only remove
        // the deleted IDs rather than overwriting the complete list
        // with an older snapshot.
        // =========================================================

        for (const auto& group :
            groupsToApply)
        {
            if (!group)
            {
                continue;
            }


            group->RemoveProfileBatch(
                profileIds
            );


            const int groupId =
                group->Id();


            if (groupId >= 0)
            {
                memMap[groupId] =
                    group;
            }
        }


        return true;
    }

    bool GroupsRepo::CommitSubscriptionState(
        const std::shared_ptr<Group>& group,
        qint64 lastUpdate,
        const QString& newInfo)
    {
        if (!group)
        {
            return false;
        }


        // =====================================================
        // Read identity without changing live subscription state.
        // =====================================================

        const GroupSnapshot snapshot =
            group->Snapshot();


        if (snapshot.id < 0)
        {
            return false;
        }


        // =====================================================
        // IMPORTANT:
        //
        // Persist FIRST.
        //
        // We intentionally do NOT call:
        //
        //     group->UpdateSubscriptionState(...)
        //
        // yet.
        //
        // Therefore another reader cannot observe a successful
        // update before SQLite has accepted the state.
        // =====================================================

        {
            std::lock_guard<std::mutex>
                locker(mutex);


            const bool persisted =
                db.exec(
                    R"(
                UPDATE groups
                SET sub_last_update = ?,
                    info = ?,
                    updated_at = strftime('%s', 'now')
                WHERE id = ?
                )",

                    static_cast<long long>(
                        lastUpdate
                        ),

                    newInfo.toStdString(),

                    snapshot.id
                );


            if (!persisted)
            {
                return false;
            }


            memMap[snapshot.id] =
                group;
        }


        // =====================================================
        // SQLite commit succeeded.
        //
        // Only NOW publish the successful state into the
        // in-memory Group.
        // =====================================================

        group->UpdateSubscriptionState(
            lastUpdate,
            newInfo
        );


        return true;
    }

}
