#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"

#include "include/global/Configs.hpp"

#include <stdexcept>

namespace Configs {
    DatabaseManager::DatabaseManager(
        const std::string& dbPath)
        :
        db(dbPath)
    {
        if (!createEntityIdsTable(
            db))
        {
            throw std::runtime_error(
                "Failed to initialize entity_ids table"
            );
        }


        initializeRepos();
    }

    bool DatabaseManager::createEntityIdsTable(
        Database& db)
    {
        if (!db.exec(
            R"(
        CREATE TABLE IF NOT EXISTS entity_ids
        (
            profile_last_id INTEGER NOT NULL DEFAULT 0,
            group_last_id INTEGER NOT NULL DEFAULT 0,
            route_profile_last_id INTEGER NOT NULL DEFAULT 0
        )
        )"))
        {
            return false;
        }


        // Initialize the single counter row only when the table is empty.
        //
        // Avoid SELECT + separate conditional logic.
        return db.exec(
            R"(
        INSERT INTO entity_ids
        (
            profile_last_id,
            group_last_id,
            route_profile_last_id
        )
        SELECT
            0,
            0,
            0
        WHERE NOT EXISTS
        (
            SELECT 1
            FROM entity_ids
        )
        )"
        );
    }
    
    void DatabaseManager::initializeRepos()
    {
        // Groups first because profiles reference groups(id).
        groupsRepo =
            std::make_unique<GroupsRepo>(
                db
            );


        profilesRepo =
            std::make_unique<ProfilesRepo>(
                db
            );


        routesRepo =
            std::make_unique<RoutesRepo>(
                db
            );


        settingsRepo =
            std::make_unique<SettingsRepo>(
                db
            );
    }
}