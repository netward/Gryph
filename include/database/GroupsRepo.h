#pragma once

#include "Database.h"
#include "include/database/entities/Group.h"

#include <map>
#include <memory>
#include <mutex>

#include <QJsonObject>
#include <QList>
#include <QString>


namespace Configs
{
    class GroupsRepo
    {
    private:

        Database& db;

        mutable std::mutex
            mutex;

        mutable std::map<
            int,
            std::shared_ptr<Group>
        > memMap;


        // =====================================================
        // Serialization
        // =====================================================

        QJsonObject groupToJson(
            const GroupSnapshot& group
        ) const;


        std::shared_ptr<Group>
            groupFromJson(
                const QJsonObject& json
            ) const;


        // =====================================================
        // Persistence
        // =====================================================

        [[nodiscard]]
        bool saveToDatabase(
            const GroupSnapshot& group
        ) const;


        std::shared_ptr<Group>
            loadFromDatabase(
                int id
            ) const;


        // =====================================================
        // Schema
        // =====================================================

        [[nodiscard]]
        bool createTables() const;


        [[nodiscard]]
        bool groupsColumnExists(
            const char* columnName
        ) const;


        // =====================================================
        // ID allocation
        // =====================================================

        int NewGroupID() const;


    public:

        explicit GroupsRepo(
            Database& database
        );


        // =====================================================
        // Creation
        // =====================================================

        [[nodiscard]]
        static std::shared_ptr<Group>
            NewGroup();


        [[nodiscard]]
        bool AddGroup(
            std::shared_ptr<Group>& group
        );


        // =====================================================
        // Access
        // =====================================================

        std::shared_ptr<Group>
            GetGroup(
                int id
            ) const;


        std::shared_ptr<Group>
            CurrentGroup() const;


        [[nodiscard]]
        QList<int>
            GetAllGroupIds() const;


        // =====================================================
        // Deletion
        // =====================================================

        [[nodiscard]]
        bool DeleteGroup(
            int id
        );


        // =====================================================
        // Group tab order
        // =====================================================

        [[nodiscard]]
        QList<int>
            GetGroupsTabOrder() const;


        [[nodiscard]]
        bool SetGroupsTabOrder(
            const QList<int>& order
        );


        // =====================================================
        // Save
        //
        // IMPORTANT:
        // Do not remove this declaration.
        //
        // GroupsRepo.cpp contains:
        //
        //     bool GroupsRepo::Save(...)
        //
        // so the class declaration must contain exactly the
        // matching method.
        // =====================================================

        [[nodiscard]]
        bool Save(
            const std::shared_ptr<Group>& group
        );

        // =============================================================
        // Cross-repository atomic Profile deletion.
        //
        // Persists:
        //
        //     affected groups.profiles_json
        //         +
        //     DELETE FROM profiles
        //
        // in one SQLite transaction.
        //
        // Group in-memory state is updated only after successful COMMIT.
        // =============================================================

        [[nodiscard]]
        bool CommitProfileDeletion(
            const QList<int>& profileIds,
            const QList<std::shared_ptr<Group>>& affectedGroups
        );


        // =====================================================
        // Subscription state
        // =====================================================

        [[nodiscard]]
        bool CommitSubscriptionState(
            const std::shared_ptr<Group>& group,
            qint64 lastUpdate,
            const QString& newInfo
        );
    };

} // namespace Configs