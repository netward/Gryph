#include "include/database/Database.h"
#include <3rdparty/SQLiteCpp/include/Backup.h>
#include <set>

namespace Configs {
    void Database::maybeCheckpoint(
        int count)
    {
        const int newCount =
            writeCount.fetch_add(
                count,
                std::memory_order_relaxed
            )
            + count;

        if (newCount >=
            WAL_CHECKPOINT_AFTER_WRITES)
        {
            writeCount.store(
                0,
                std::memory_order_relaxed
            );

            checkpointWal();
        }
    }

    void Database::checkpointWal() {
        std::lock_guard<std::recursive_mutex>
            locker(db_mutex);
        try {
            db.exec("PRAGMA wal_checkpoint(TRUNCATE)");
        } catch (std::exception& e) {
            std::cerr << "DB WAL checkpoint error: " << e.what() << std::endl;
        }
    }

    void Database::execDeleteByIdInChunk(
        const std::string& table,
        const std::string& idColumn,
        const std::vector<int>& ids)
    {
        if (ids.empty())
        {
            return;
        }


        std::string sql =
            "DELETE FROM "
            + table
            + " WHERE "
            + idColumn
            + " IN (";


        for (size_t i = 0;
            i < ids.size();
            ++i)
        {
            if (i > 0)
            {
                sql += ",";
            }

            sql +=
                std::to_string(
                    ids[i]
                );
        }


        sql += ")";


        // Do NOT catch here.
        // Owner transaction must see the exception.
        db.exec(
            sql
        );
    }

    void Database::execBatchSettingsReplaceChunk(
        const std::vector<
        std::pair<
        std::string,
        std::string
        >
        >& keyValues)
    {
        if (keyValues.empty())
        {
            return;
        }


        std::string sql =
            "INSERT OR REPLACE INTO settings "
            "(key, value) VALUES ";


        for (size_t i = 0;
            i < keyValues.size();
            ++i)
        {
            if (i > 0)
            {
                sql += ",";
            }

            sql += "(?,?)";
        }


        SQLite::Statement stmt(
            db,
            sql
        );


        for (size_t i = 0;
            i < keyValues.size();
            ++i)
        {
            stmt.bind(
                static_cast<int>(
                    2 * i + 1
                    ),
                keyValues[i].first
            );

            stmt.bind(
                static_cast<int>(
                    2 * i + 2
                    ),
                keyValues[i].second
            );
        }


        // Do NOT catch here.
        stmt.exec();
    }

    void Database::execBatchInsertIntPairsChunk(
        const std::string& table,
        const std::string& colA,
        const std::string& colB,
        const std::vector<int>& pairs)
    {
        if (pairs.size() < 2 ||
            pairs.size() % 2 != 0)
        {
            return;
        }


        std::string sql =
            "INSERT INTO "
            + table
            + " ("
            + colA
            + ","
            + colB
            + ") VALUES ";


        const size_t count =
            pairs.size() / 2;


        for (size_t i = 0;
            i < count;
            ++i)
        {
            if (i > 0)
            {
                sql += ",";
            }

            sql += "(?,?)";
        }


        SQLite::Statement stmt(
            db,
            sql
        );


        for (size_t i = 0;
            i < pairs.size();
            ++i)
        {
            stmt.bind(
                static_cast<int>(
                    i + 1
                    ),
                pairs[i]
            );
        }


        stmt.exec();
    }

    void Database::execBatchInsertProfilesChunk(
        const std::vector<ProfileInsertRow>& rows)
    {
        if (rows.empty())
        {
            return;
        }


        const size_t count =
            rows.size();


        std::string sql =
            "INSERT INTO profiles "
            "("
            "id, "
            "type, "
            "name, "
            "gid, "
            "latency, "
            "dl_speed, "
            "ul_speed, "
            "test_country, "
            "ip_out, "
            "outbound_json, "
            "traffic_dl, "
            "traffic_up"
            ") "
            "VALUES ";


        for (size_t i = 0;
            i < count;
            ++i)
        {
            if (i > 0)
            {
                sql += ",";
            }


            sql +=
                "(?,?,?,?,?,?,?,?,?,?,?,?)";
        }


        SQLite::Statement stmt(
            db,
            sql
        );


        int index = 1;


        for (const auto& row :
            rows)
        {
            stmt.bind(
                index++,
                row.id
            );

            stmt.bind(
                index++,
                row.type
            );

            stmt.bind(
                index++,
                row.name
            );

            stmt.bind(
                index++,
                row.gid
            );

            stmt.bind(
                index++,
                row.latency
            );

            stmt.bind(
                index++,
                row.dl_speed
            );

            stmt.bind(
                index++,
                row.ul_speed
            );

            stmt.bind(
                index++,
                row.test_country
            );

            stmt.bind(
                index++,
                row.ip_out
            );

            stmt.bind(
                index++,
                row.outbound_json
            );

            stmt.bind(
                index++,
                static_cast<int64_t>(
                    row.traffic_dl
                    )
            );

            stmt.bind(
                index++,
                static_cast<int64_t>(
                    row.traffic_up
                    )
            );
        }


        // IMPORTANT:
        //
        // Do not catch SQLite exception here.
        // The owner transaction must see it.
        stmt.exec();
    }

    bool Database::execBatchInsertProfiles(
        const std::vector<ProfileInsertRow>& rows)
    {
        if (rows.empty())
        {
            return true;
        }


        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        bool transactionStarted =
            false;


        try
        {
            // =====================================================
            // One SQLite transaction for the WHOLE batch.
            // =====================================================

            db.exec(
                "BEGIN IMMEDIATE"
            );


            transactionStarted =
                true;


            // execBatchInsertProfiles0() may split the batch
            // into multiple SQL statements, but all of them
            // are now inside one SQLite transaction.
            execBatchInsertProfiles0(
                rows
            );


            db.exec(
                "COMMIT"
            );


            transactionStarted =
                false;


            // Count committed writes only after COMMIT.
            maybeCheckpoint(
                static_cast<int>(
                    rows.size()
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            if (transactionStarted)
            {
                try
                {
                    db.exec(
                        "ROLLBACK"
                    );
                }
                catch (...)
                {
                    // Preserve the original exception.
                }
            }


            NotifyError(
                "execBatchInsertProfiles",
                e
            );


            return false;
        }
    }

    bool Database::execDeleteByIdIn(
        const std::string& table,
        const std::string& idColumn,
        const std::vector<int>& ids)
    {
        if (ids.empty())
        {
            return true;
        }


        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        bool transactionStarted =
            false;


        try
        {
            db.exec(
                "BEGIN IMMEDIATE"
            );

            transactionStarted =
                true;


            execDeleteByIdIn0(
                table,
                idColumn,
                ids
            );


            db.exec(
                "COMMIT"
            );

            transactionStarted =
                false;


            maybeCheckpoint(
                static_cast<int>(
                    ids.size()
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            if (transactionStarted)
            {
                try
                {
                    db.exec(
                        "ROLLBACK"
                    );
                }
                catch (...)
                {
                }
            }


            NotifyError(
                "execDeleteByIdIn for "
                + table,
                e
            );


            return false;
        }
    }

    bool Database::execBatchSettingsReplace(
        const std::vector<
        std::pair<
        std::string,
        std::string
        >
        >& keyValues)
    {
        if (keyValues.empty())
        {
            return true;
        }


        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        bool transactionStarted =
            false;


        try
        {
            db.exec(
                "BEGIN IMMEDIATE"
            );

            transactionStarted =
                true;


            execBatchSettingsReplace0(
                keyValues
            );


            db.exec(
                "COMMIT"
            );

            transactionStarted =
                false;


            maybeCheckpoint(
                static_cast<int>(
                    keyValues.size()
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            if (transactionStarted)
            {
                try
                {
                    db.exec(
                        "ROLLBACK"
                    );
                }
                catch (...)
                {
                }
            }


            NotifyError(
                "execBatchSettingsReplace",
                e
            );


            return false;
        }
    }

    bool Database::execBatchInsertIntPairs(
        const std::string& table,
        const std::string& colA,
        const std::string& colB,
        const std::vector<int>& pairs)
    {
        if (pairs.empty())
        {
            return true;
        }


        if (pairs.size() < 2 ||
            pairs.size() % 2 != 0)
        {
            MW_show_log(
                "Database::execBatchInsertIntPairs: "
                "invalid pair vector"
            );

            return false;
        }


        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        bool transactionStarted =
            false;


        try
        {
            db.exec(
                "BEGIN IMMEDIATE"
            );

            transactionStarted =
                true;


            execBatchInsertIntPairs0(
                table,
                colA,
                colB,
                pairs
            );


            db.exec(
                "COMMIT"
            );

            transactionStarted =
                false;


            maybeCheckpoint(
                static_cast<int>(
                    pairs.size() / 2
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            if (transactionStarted)
            {
                try
                {
                    db.exec(
                        "ROLLBACK"
                    );
                }
                catch (...)
                {
                }
            }


            NotifyError(
                "execBatchInsertIntPairs for "
                + table,
                e
            );


            return false;
        }
    }

    bool Database::execBatchReplaceProfiles(
        const std::vector<ProfileInsertRow>& rows)
    {
        if (rows.empty())
        {
            return true;
        }


        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        bool transactionStarted =
            false;


        try
        {
            db.exec(
                "BEGIN IMMEDIATE"
            );

            transactionStarted =
                true;


            execBatchReplaceProfiles0(
                rows
            );


            db.exec(
                "COMMIT"
            );

            transactionStarted =
                false;


            maybeCheckpoint(
                static_cast<int>(
                    rows.size()
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            if (transactionStarted)
            {
                try
                {
                    db.exec(
                        "ROLLBACK"
                    );
                }
                catch (...)
                {
                }
            }


            NotifyError(
                "execBatchReplaceProfiles",
                e
            );


            return false;
        }
    }

    bool Database::deleteProfilesAtomic(
        const std::vector<int>& ids)
    {
        if (ids.empty())
        {
            return true;
        }


        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        bool transactionStarted =
            false;


        try
        {
            db.exec(
                "BEGIN IMMEDIATE"
            );


            transactionStarted =
                true;


            constexpr size_t chunkSize =
                500;


            for (size_t offset = 0;
                offset < ids.size();
                offset += chunkSize)
            {
                const size_t end =
                    std::min(
                        offset + chunkSize,
                        ids.size()
                    );


                std::string sql =
                    "DELETE FROM profiles "
                    "WHERE id IN (";


                for (size_t i = offset;
                    i < end;
                    ++i)
                {
                    if (i > offset)
                    {
                        sql += ",";
                    }


                    sql +=
                        std::to_string(
                            ids[i]
                        );
                }


                sql += ")";


                db.exec(
                    sql
                );
            }


            db.exec(
                "COMMIT"
            );


            transactionStarted =
                false;


            maybeCheckpoint(
                static_cast<int>(
                    ids.size()
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            if (transactionStarted)
            {
                try
                {
                    db.exec(
                        "ROLLBACK"
                    );
                }
                catch (...)
                {
                }
            }


            NotifyError(
                "deleteProfilesAtomic",
                e
            );


            return false;
        }
    }

    void Database::execBatchReplaceProfilesChunk(
        const std::vector<ProfileInsertRow>& rows)
    {
        if (rows.empty())
        {
            return;
        }


        const size_t count =
            rows.size();


        std::string sql =
            "INSERT OR REPLACE INTO profiles "
            "("
            "id, "
            "type, "
            "name, "
            "gid, "
            "latency, "
            "dl_speed, "
            "ul_speed, "
            "test_country, "
            "ip_out, "
            "outbound_json, "
            "traffic_dl, "
            "traffic_up"
            ") VALUES ";


        for (size_t i = 0;
            i < count;
            ++i)
        {
            if (i > 0)
            {
                sql += ",";
            }

            sql +=
                "(?,?,?,?,?,?,?,?,?,?,?,?)";
        }


        SQLite::Statement stmt(
            db,
            sql
        );


        int index = 1;


        for (const auto& row :
            rows)
        {
            stmt.bind(
                index++,
                row.id
            );

            stmt.bind(
                index++,
                row.type
            );

            stmt.bind(
                index++,
                row.name
            );

            stmt.bind(
                index++,
                row.gid
            );

            stmt.bind(
                index++,
                row.latency
            );

            stmt.bind(
                index++,
                row.dl_speed
            );

            stmt.bind(
                index++,
                row.ul_speed
            );

            stmt.bind(
                index++,
                row.test_country
            );

            stmt.bind(
                index++,
                row.ip_out
            );

            stmt.bind(
                index++,
                row.outbound_json
            );

            stmt.bind(
                index++,
                static_cast<int64_t>(
                    row.traffic_dl
                    )
            );

            stmt.bind(
                index++,
                static_cast<int64_t>(
                    row.traffic_up
                    )
            );
        }


        stmt.exec();
    }

    void Database::backupTo(const std::string& destPath) {
        std::lock_guard<std::recursive_mutex>
            locker(db_mutex);
        SQLite::Database destDb(destPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        SQLite::Backup backup(destDb, db);
        backup.executeStep(-1);
    }

    void Database::restoreFrom(const std::string& srcPath) {
        std::lock_guard<std::recursive_mutex>
            locker(db_mutex);
        SQLite::Database srcDb(srcPath, SQLite::OPEN_READONLY);
        SQLite::Backup restore(db, srcDb);
        restore.executeStep(-1);
    }

    namespace {
        // Tables that make up each logical category. Delete order matters when
        // foreign keys are on; we copy with them off, but keep child-first order
        // for clarity and so re-enabling FK checks afterwards stays consistent.
        const std::vector<std::string> kProfileTables = {"profiles", "groups_order", "groups"};
        const std::vector<std::string> kRouteTables = {"route_rules", "route_profiles"};
        const std::vector<std::string> kSettingsTables = {"settings"};

        std::vector<std::string> tableColumns(SQLite::Database& d, const std::string& schema, const std::string& table) {
            std::vector<std::string> cols;
            // schema/table are internal constants, not user input -> safe to inline.
            SQLite::Statement q(d, "PRAGMA " + schema + ".table_info(" + table + ")");
            while (q.executeStep()) cols.emplace_back(q.getColumn(1).getText());
            return cols;
        }

        bool tableExists(SQLite::Database& d, const std::string& schema, const std::string& table) {
            SQLite::Statement q(d, "SELECT 1 FROM " + schema + ".sqlite_master WHERE type='table' AND name=?");
            q.bind(1, table);
            return q.executeStep();
        }

        // Replace every row of main.<table> with the rows from bak.<table>,
        // copying only the columns that exist in both schemas.
        void copyTable(SQLite::Database& d, const std::string& table) {
            if (!tableExists(d, "main", table) || !tableExists(d, "bak", table)) return;

            const auto mainCols = tableColumns(d, "main", table);
            const auto bakColsVec = tableColumns(d, "bak", table);
            const std::set<std::string> bakCols(bakColsVec.begin(), bakColsVec.end());

            std::string colList;
            for (const auto& c : mainCols) {
                if (bakCols.count(c) == 0) continue;
                if (!colList.empty()) colList += ",";
                colList += "\"" + c + "\"";
            }
            if (colList.empty()) return;

            d.exec("DELETE FROM main." + table);
            d.exec("INSERT INTO main." + table + " (" + colList + ") SELECT " + colList + " FROM bak." + table);
        }
    }

    void Database::backupSelective(const std::string& destPath, const BackupParts& parts) {
        // Take a full, WAL-safe snapshot first (same mechanism as backupTo),
        // then strip the categories the user did not select. entity_ids is
        // always kept so profile/route IDs stay consistent on restore.
        {
            std::lock_guard<std::recursive_mutex>
                locker(db_mutex);
            SQLite::Database destDb(destPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
            SQLite::Backup backup(destDb, db);
            backup.executeStep(-1);
        }

        SQLite::Database dest(destPath, SQLite::OPEN_READWRITE);
        auto wipe = [&](const std::vector<std::string>& tables) {
            for (const auto& t : tables) {
                try {
                    if (tableExists(dest, "main", t)) dest.exec("DELETE FROM " + t);
                } catch (...) {}
            }
        };
        if (!parts.profiles) wipe(kProfileTables);
        if (!parts.routes) wipe(kRouteTables);
        if (!parts.settings) wipe(kSettingsTables);
        try { dest.exec("VACUUM"); } catch (...) {}
    }

    void Database::restoreSelective(
        const std::string& srcPath,
        const BackupParts& parts)
    {
        if (!parts.anyDb()) {
            return;
        }

        // The whole restore operation must be atomic
        // relative to all other users of this Database.
        std::lock_guard<std::recursive_mutex>
            locker(db_mutex);

        // -------------------------------------------------
        // Attach backup database
        // -------------------------------------------------

        {
            SQLite::Statement attach(
                db,
                "ATTACH DATABASE ? AS bak"
            );

            attach.bind(
                1,
                srcPath
            );

            attach.exec();
        }

        try {

            // foreign_keys must be changed outside
            // the transaction.
            db.exec(
                "PRAGMA foreign_keys = OFF"
            );

            db.exec(
                "BEGIN IMMEDIATE"
            );

            // -------------------------------------------------
            // Restore selected tables
            // -------------------------------------------------

            if (parts.profiles) {

                for (const auto& table :
                    kProfileTables)
                {
                    copyTable(
                        db,
                        table
                    );
                }
            }

            if (parts.routes) {

                for (const auto& table :
                    kRouteTables)
                {
                    copyTable(
                        db,
                        table
                    );
                }
            }

            if (parts.settings) {

                for (const auto& table :
                    kSettingsTables)
                {
                    copyTable(
                        db,
                        table
                    );
                }
            }

            // -------------------------------------------------
            // Repair ID counters
            // -------------------------------------------------

            if (parts.profiles ||
                parts.routes)
            {
                const bool bakIds =
                    tableExists(
                        db,
                        "bak",
                        "entity_ids"
                    );

                db.exec(
                    "UPDATE entity_ids SET "

                    "profile_last_id = MAX("
                    "profile_last_id,"
                    "(SELECT COALESCE(MAX(id),0) "
                    "FROM profiles)"
                    +

                    std::string(
                        bakIds
                        ? ",(SELECT COALESCE("
                        "MAX(profile_last_id),0) "
                        "FROM bak.entity_ids)"
                        : ""
                    )

                    + "),"

                    "group_last_id = MAX("
                    "group_last_id,"
                    "(SELECT COALESCE(MAX(id),0) "
                    "FROM groups)"
                    +

                    std::string(
                        bakIds
                        ? ",(SELECT COALESCE("
                        "MAX(group_last_id),0) "
                        "FROM bak.entity_ids)"
                        : ""
                    )

                    + "),"

                    "route_profile_last_id = MAX("
                    "route_profile_last_id,"
                    "(SELECT COALESCE(MAX(id),0) "
                    "FROM route_profiles)"
                    +

                    std::string(
                        bakIds
                        ? ",(SELECT COALESCE("
                        "MAX(route_profile_last_id),0) "
                        "FROM bak.entity_ids)"
                        : ""
                    )

                    + ")"
                );
            }

            db.exec(
                "COMMIT"
            );
        }
        catch (...) {

            try {
                db.exec(
                    "ROLLBACK"
                );
            }
            catch (...) {
            }

            try {
                db.exec(
                    "PRAGMA foreign_keys = ON"
                );
            }
            catch (...) {
            }

            try {
                db.exec(
                    "DETACH DATABASE bak"
                );
            }
            catch (...) {
            }

            throw;
        }

        // -------------------------------------------------
        // Cleanup
        // -------------------------------------------------

        db.exec(
            "PRAGMA foreign_keys = ON"
        );

        db.exec(
            "DETACH DATABASE bak"
        );

        checkpointWal();
    }

    bool Database::execBatchUpdateProfileTraffic(
        const std::vector<ProfileTrafficRow>& rows)
    {
        if (rows.empty())
        {
            return true;
        }


        try
        {
            std::lock_guard<
                std::recursive_mutex
            > dbLocker(
                db_mutex
            );


            SQLite::Transaction transaction(
                db,
                SQLite::TransactionBehavior::IMMEDIATE
            );


            SQLite::Statement stmt(
                db,
                "UPDATE profiles "
                "SET traffic_dl = ?, "
                "traffic_up = ? "
                "WHERE id = ?"
            );


            int updatedRows =
                0;


            for (const auto& row :
                rows)
            {
                if (row.id < 0)
                {
                    continue;
                }


                stmt.bind(
                    1,
                    static_cast<int64_t>(
                        row.traffic_dl
                        )
                );

                stmt.bind(
                    2,
                    static_cast<int64_t>(
                        row.traffic_up
                        )
                );

                stmt.bind(
                    3,
                    row.id
                );


                stmt.exec();

                stmt.reset();

                ++updatedRows;
            }


            transaction.commit();


            if (updatedRows > 0)
            {
                maybeCheckpoint(
                    updatedRows
                );
            }


            return true;
        }
        catch (std::exception& e)
        {
            NotifyError(
                "execBatchUpdateProfileTraffic",
                e
            );


            return false;
        }
    }
}
